#include "widgets/GpuSpectralMatrix.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace prism::widgets {
GpuSpectralMatrix::GpuSpectralMatrix(std::uint32_t columns, double sampleRate)
    : columns_(columns), sampleRate_(sampleRate), fft_(fftSize), instant_(columns),
      states_(columns), bands_(static_cast<std::size_t>(columns) * 4),
      high_(std::min(20000.0, sampleRate * 0.5)) {
    if (columns == 0 || sampleRate <= 40) {
        throw std::invalid_argument(
            "Spectral matrix requires columns and a sample rate above 40 Hz");
    }
    double sum = 0;
    for (std::size_t n = 0; n < fftSize; ++n) {
        const double t = 2 * std::numbers::pi * static_cast<double>(n) / (fftSize - 1);
        window_[n] =
            0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
        sum += window_[n];
    }
    for (double &value : window_) {
        value *= 2 / sum;
    }
    const double binWidth = sampleRate_ / fftSize;
    const double step = std::log(high_ / low_) / columns_;
    const double octaveWidth = std::log2(high_ / low_) / columns_;
    for (std::uint32_t i = 0; i < columns_; ++i) {
        const double frequency = low_ * std::exp(step * (i + 0.5));
        Band band{frequency, low_ * std::exp(step * i), low_ * std::exp(step * (i + 1)),
                  weights_.size(), 0};
        const double lower = frequency * std::exp2(-octaveWidth);
        const double upper = frequency * std::exp2(octaveWidth);
        const auto begin = static_cast<std::size_t>(std::max(1.0, std::ceil(lower / binWidth)));
        const auto end = static_cast<std::size_t>(
            std::min(static_cast<double>(fftSize / 2), std::floor(upper / binWidth)));
        double total = 0;
        for (std::size_t k = begin; k <= end; ++k) {
            const double weight =
                std::max(0.0, 1 - std::abs(std::log2(k * binWidth / frequency)) / octaveWidth);
            weights_.push_back({k, weight});
            total += weight;
        }
        if (total == 0) {
            weights_.resize(band.first);
            const auto k = static_cast<std::size_t>(std::clamp(
                std::round(frequency / binWidth), 1.0, static_cast<double>(fftSize / 2)));
            weights_.push_back({k, 1});
            total = 1;
        }
        band.count = weights_.size() - band.first;
        for (std::size_t j = band.first; j < weights_.size(); ++j) {
            weights_[j].weight /= total;
        }
        bank_.push_back(band);
    }
}
double GpuSpectralMatrix::columnFrequency(std::uint32_t column) const noexcept {
    return bank_[column].frequency;
}
void GpuSpectralMatrix::stepNormalized(std::span<const float> levels, double dt) noexcept {
    for (std::uint32_t i = 0; i < columns_; ++i) {
        states_[i].step(i < levels.size() ? levels[i] : 0, dt);
        bands_[4 * i] = static_cast<float>(states_[i].height);
        bands_[4 * i + 1] = static_cast<float>(states_[i].peak);
        bands_[4 * i + 2] = static_cast<float>(states_[i].flux);
        bands_[4 * i + 3] = 1;
    }
}
void GpuSpectralMatrix::finishSpectrum(double dt, double tiltDb) {
    for (std::uint32_t i = 0; i < columns_; ++i) {
        const auto &band = bank_[i];
        double power = 0;
        for (std::size_t j = 0; j < band.count; ++j) {
            const auto &bin = weights_[band.first + j];
            power += bin.weight * std::norm(spectrum_[bin.bin]);
        }
        const double tilted =
            power * std::pow(band.frequency / 1000, tiltDb / (10 * std::log10(2.0)));
        const double db = 10 * std::log10(tilted + 2.51188643150958e-7);
        instant_[i] = static_cast<float>(std::clamp((db + 66) / 66, 0.0, 1.0));
    }
    stepNormalized(instant_, dt);
}
void GpuSpectralMatrix::analyze(std::span<const float> block, double dt, double tiltDb) {
    for (std::size_t n = 0; n < fftSize; ++n) {
        input_[n] = (n < block.size() ? block[n] : 0) * window_[n];
    }
    fft_.forward(input_, spectrum_);
    finishSpectrum(dt, tiltDb);
}
void GpuSpectralMatrix::consume(double dt) {
    auto remaining = audio_.available();
    const bool received = remaining != 0;
    while (remaining > 0) {
        const auto count = audio_.pop(incoming_.data(), std::min(remaining, incoming_.size()));
        for (std::size_t i = 0; i < count; ++i) {
            latest_[write_] = incoming_[i];
            write_ = (write_ + 1) & (fftSize - 1);
        }
        remaining -= count;
    }
    if (received) {
        for (std::size_t n = 0; n < fftSize; ++n) {
            input_[n] = latest_[(write_ + n) & (fftSize - 1)] * window_[n];
        }
        fft_.forward(input_, spectrum_);
        finishSpectrum(dt, 3.5);
    } else {
        stepNormalized(instant_, dt);
    }
}
void GpuSpectralMatrix::analyzePartials(std::span<const float> amplitudes, double fundamental,
                                        double dt, double tiltDb) {
    if (!(fundamental > 0)) {
        throw std::invalid_argument("Partial fundamental must be positive");
    }
    for (std::uint32_t i = 0; i < columns_; ++i) {
        const double lower = bank_[i].lower / fundamental;
        const double upper = bank_[i].upper / fundamental;
        double power = 0, weights = 0;
        const auto begin = static_cast<std::size_t>(std::max(1.0, std::ceil(lower - 0.5)));
        const auto end = static_cast<std::size_t>(
            std::min(static_cast<double>(amplitudes.size()), std::floor(upper + 0.5)));
        for (std::size_t k = begin; k <= end; ++k) {
            const double weight =
                std::max(0.0, std::min(upper, k + 0.5) - std::max(lower, k - 0.5));
            const double amplitude = amplitudes[k - 1];
            power += weight * amplitude * amplitude;
            weights += weight;
        }
        const double tilted = power / std::max(weights, 1.0) *
                              std::pow(columnFrequency(i) / 1000, tiltDb / (10 * std::log10(2.0)));
        const double db = 10 * std::log10(tilted + 2.51188643150958e-7);
        instant_[i] = static_cast<float>(std::clamp((db + 66) / 66, 0.0, 1.0));
    }
    stepNormalized(instant_, dt);
}
void GpuSpectralMatrix::stop() noexcept {
    auto remaining = audio_.available();
    while (remaining > 0) {
        remaining -= audio_.pop(incoming_.data(), std::min(remaining, incoming_.size()));
    }
    latest_.fill(0);
    std::fill(instant_.begin(), instant_.end(), 0);
}
bool GpuSpectralMatrix::active() const noexcept {
    return audio_.available() != 0 || std::any_of(states_.begin(), states_.end(),
                                                  [](const auto &state) { return state.active(); });
}
void GpuSpectralMatrix::draw(render::DrawList &list, render::RectF rect, std::uint32_t slot,
                             std::uint32_t paletteRow, float rows) const {
    HalftoneStyle style;
    style.columns = static_cast<float>(columns_);
    style.rows = rows;
    style.gutterX = 0.24f;
    style.gutterY = 0.36f;
    style.radius = 0.32f;
    style.minimumWidth = 1;
    style.screenMix = 0;
    auto h = halftonePayload(rect, style, paletteRow, render::noTexture);
    h.modeFlags = render::halftoneFlags::spectralMatrix;
    list.halftoneField(h, slot);
}
}
