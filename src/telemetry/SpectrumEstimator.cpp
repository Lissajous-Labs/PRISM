#include "telemetry/SpectrumEstimator.h"

#include "math/Pchip.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace prism::telemetry {
namespace {
constexpr std::array<double, 4> blackmanHarris{0.35875, 0.48829, 0.14128, 0.01168};
constexpr double powerFloor = 1e-14;
}

SpectrumEstimator::SpectrumEstimator(const SpectrumSettings &settings)
    : settings_(settings), taps_(designHalfBand()),
      stages_{HalfBandDecimator(taps_), HalfBandDecimator(taps_), HalfBandDecimator(taps_)} {
    for (const SpectrumBand &spec : spectrumBands) {
        Band band{settings_.sampleRate / spec.decimation,
                  std::vector<double>(spec.size),
                  0,
                  std::vector<double>(spec.size, 0.0),
                  0,
                  std::vector<double>(spec.size),
                  std::vector<std::complex<double>>(spec.size / 2 + 1),
                  std::vector<double>(spec.size / 2 + 1, 0.0),
                  Fft(spec.size)};
        for (std::uint32_t n = 0; n < spec.size; ++n) {
            const double phase = 2 * std::numbers::pi * n / spec.size;
            band.window[n] = blackmanHarris[0] - blackmanHarris[1] * std::cos(phase) +
                             blackmanHarris[2] * std::cos(2 * phase) -
                             blackmanHarris[3] * std::cos(3 * phase);
            band.windowPower += band.window[n] * band.window[n];
        }
        bands_.push_back(std::move(band));
    }

    const std::uint32_t count = std::max<std::uint32_t>(settings_.knots, 2);
    const double ratio = std::log(settings_.highHz / settings_.lowHz) / (count - 1);
    for (std::uint32_t i = 0; i < count; ++i) {
        frequencies_.push_back(settings_.lowHz * std::exp(ratio * i));
        positions_.push_back(static_cast<double>(i) / (count - 1));
    }
    for (std::uint32_t i = 0; i <= count; ++i) {
        edges_.push_back(settings_.lowHz * std::exp(ratio * (static_cast<double>(i) - 0.5)));
    }
    power_.assign(count, 0.0);
    instant_.assign(count, settings_.floorDb);
    levels_.assign(count, settings_.floorDb);
    peaks_.assign(count, settings_.floorDb);
    holds_.assign(count, 0.0);
    slopes_.assign(count, 0.0);
}

double SpectrumEstimator::binWidth(std::size_t band) const noexcept {
    return bands_[band].rate / spectrumBands[band].size;
}

void SpectrumEstimator::feed(std::size_t band, double value) noexcept {
    Band &b = bands_[band];
    b.history[b.written % b.history.size()] = value;
    ++b.written;
}

void SpectrumEstimator::push(std::span<const float> samples) noexcept {
    for (const float sample : samples) {
        double value = sample;
        feed(3, value);
        for (std::size_t stage = 0; stage < stages_.size(); ++stage) {
            if (!stages_[stage].push(value, value)) {
                break;
            }
            feed(2 - stage, value);
        }
    }
}

void SpectrumEstimator::transform(Band &band) noexcept {
    const std::size_t size = band.history.size();
    const std::size_t start = band.written % size;
    for (std::size_t n = 0; n < size; ++n) {
        band.input[n] = band.history[(start + n) % size] * band.window[n];
    }
    band.fft.forward(band.input, band.spectrum);

    const double scale = 1.0 / (band.rate * band.windowPower);
    for (std::size_t k = 0; k < band.density.size(); ++k) {
        const double edge = k == 0 || k + 1 == band.density.size() ? 1.0 : 2.0;
        band.density[k] = edge * std::norm(band.spectrum[k]) * scale;
    }
}

double SpectrumEstimator::average(const Band &band, double low, double high) const noexcept {
    const double width = band.rate / band.history.size();
    const auto last = static_cast<std::ptrdiff_t>(band.density.size()) - 1;
    const auto first = std::clamp<std::ptrdiff_t>(
        static_cast<std::ptrdiff_t>(std::floor(low / width + 0.5)), 0, last);
    const auto final = std::clamp<std::ptrdiff_t>(
        static_cast<std::ptrdiff_t>(std::floor(high / width + 0.5)), 0, last);
    double sum = 0;
    for (std::ptrdiff_t k = first; k <= final; ++k) {
        const double binLow = (static_cast<double>(k) - 0.5) * width;
        const double binHigh = (static_cast<double>(k) + 0.5) * width;
        const double overlap = std::min(high, binHigh) - std::max(low, binLow);
        if (overlap > 0) {
            sum += overlap * band.density[static_cast<std::size_t>(k)];
        }
    }
    return sum / (high - low);
}

double SpectrumEstimator::weight(std::size_t band, double hz) const noexcept {
    const auto rise = [](double hz, double edge) {
        const double u = (std::log2(hz / edge) + crossoverOctaves) / (2 * crossoverOctaves);
        if (u <= 0) {
            return 0.0;
        }
        if (u >= 1) {
            return 1.0;
        }
        return 0.5 - 0.5 * std::cos(std::numbers::pi * u);
    };
    const double fromBelow = band == 0 ? 1.0 : rise(hz, spectrumBands[band].low);
    const double intoAbove =
        band + 1 == spectrumBands.size() ? 1.0 : 1.0 - rise(hz, spectrumBands[band].high);
    return fromBelow * intoAbove;
}

bool SpectrumEstimator::analyze(double dt) noexcept {
    for (const Band &band : bands_) {
        if (band.written < band.history.size()) {
            return false;
        }
    }
    for (Band &band : bands_) {
        transform(band);
    }

    for (std::size_t i = 0; i < frequencies_.size(); ++i) {
        double sum = 0;
        for (std::size_t b = 0; b < bands_.size(); ++b) {
            const double w = weight(b, frequencies_[i]);
            if (w > 0) {
                sum += w * average(bands_[b], edges_[i], edges_[i + 1]);
            }
        }
        power_[i] = sum;
        instant_[i] = std::max(settings_.floorDb,
                               10 * std::log10(sum * settings_.sampleRate / 2 + powerFloor) +
                                   settings_.tiltDbPerOctave * std::log2(frequencies_[i] / 1000));
    }
    smooth(dt);
    return true;
}

void SpectrumEstimator::smooth(double dt) noexcept {
    const double attack = std::exp(-dt / settings_.attackSeconds);
    const double release = std::exp(-dt / settings_.releaseSeconds);
    for (std::size_t i = 0; i < instant_.size(); ++i) {
        const double target = instant_[i];
        if (!primed_) {
            levels_[i] = target;
        } else {
            const double alpha = target > levels_[i] ? attack : release;
            levels_[i] = alpha * levels_[i] + (1 - alpha) * target;
        }

        if (levels_[i] >= peaks_[i] || !primed_) {
            peaks_[i] = levels_[i];
            holds_[i] = settings_.holdSeconds;
            continue;
        }
        const double held = std::min(holds_[i], dt);
        holds_[i] -= held;
        peaks_[i] = std::max(levels_[i], peaks_[i] - settings_.peakFallDbPerSecond * (dt - held));
    }
    primed_ = true;
    math::pchipSlopes(positions_, levels_, slopes_);
}
}
