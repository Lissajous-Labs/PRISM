#include "telemetry/LoudnessMeter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace prism::telemetry {
namespace {
constexpr double absoluteGate = -70.0;
constexpr double histogramFloor = -70.0;
constexpr double histogramStep = 0.01;

double energyToLufs(double energy) noexcept {
    return energy > 0 ? -0.691 + 10 * std::log10(energy) : static_cast<double>(silence);
}

double besselI0(double x) noexcept {
    double sum = 1;
    double term = 1;
    for (int k = 1; k < 32; ++k) {
        term *= (x / (2 * k)) * (x / (2 * k));
        sum += term;
    }
    return sum;
}
}

Biquad Biquad::kWeightingShelf(double sampleRate) noexcept {
    const double f0 = 1681.974450955533;
    const double gain = 3.999843853973347;
    const double q = 0.7071752369554196;
    const double k = std::tan(std::numbers::pi * f0 / sampleRate);
    const double vh = std::pow(10.0, gain / 20);
    const double vb = std::pow(vh, 0.4996667741545416);
    const double a0 = 1 + k / q + k * k;
    return {(vh + vb * k / q + k * k) / a0, 2 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
            2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0};
}

Biquad Biquad::kWeightingHighpass(double sampleRate) noexcept {
    const double f0 = 38.13547087602444;
    const double q = 0.5003270373238773;
    const double k = std::tan(std::numbers::pi * f0 / sampleRate);
    const double a0 = 1 + k / q + k * k;
    return {1, -2, 1, 2 * (k * k - 1) / a0, (1 - k / q + k * k) / a0};
}

LoudnessMeter::LoudnessMeter(double sampleRate)
    : subBlock_(static_cast<std::size_t>(std::llround(sampleRate / 10))),
      shelf_(Biquad::kWeightingShelf(sampleRate)),
      highpass_(Biquad::kWeightingHighpass(sampleRate)), histogramCount_(histogramBins, 0),
      histogramEnergy_(histogramBins, 0) {
    constexpr std::size_t length = oversampling * tapsPerPhase;
    constexpr double beta = 8.0;
    const double center = (static_cast<double>(length) - 1) / 2;
    for (std::size_t n = 0; n < length; ++n) {
        const double t = (static_cast<double>(n) - center) / oversampling;
        const double sinc = t == 0 ? 1 : std::sin(std::numbers::pi * t) / (std::numbers::pi * t);
        const double r = (static_cast<double>(n) - center) / center;
        taps_[n] = sinc * besselI0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / besselI0(beta);
    }
    for (std::size_t phase = 0; phase < oversampling; ++phase) {
        double sum = 0;
        for (std::size_t k = 0; k < tapsPerPhase; ++k) {
            sum += taps_[phase + oversampling * k];
        }
        for (std::size_t k = 0; k < tapsPerPhase; ++k) {
            taps_[phase + oversampling * k] /= sum;
        }
    }
    reset();
}

void LoudnessMeter::reset() noexcept {
    shelfState_ = {};
    highpassState_ = {};
    history_ = {};
    historyIndex_ = 0;
    subBlockFill_ = 0;
    subWeighted_ = {};
    subPlain_ = {};
    weightedRing_ = {};
    plainRing_ = {};
    ringCount_ = 0;
    ringIndex_ = 0;
    std::fill(histogramCount_.begin(), histogramCount_.end(), 0);
    std::fill(histogramEnergy_.begin(), histogramEnergy_.end(), 0.0);
    peak_ = 0;
    for (auto &value : published_) {
        value.store(silence, std::memory_order_relaxed);
    }
}

float LoudnessMeter::truePeakSample(std::size_t channel, float sample) noexcept {
    auto &history = history_[channel];
    history[historyIndex_] = sample;
    float peak = 0;
    for (std::size_t phase = 0; phase < oversampling; ++phase) {
        double acc = 0;
        for (std::size_t k = 0; k < tapsPerPhase; ++k) {
            acc += taps_[phase + oversampling * k] *
                   history[(historyIndex_ + tapsPerPhase - k) % tapsPerPhase];
        }
        peak = std::max(peak, static_cast<float>(std::abs(acc)));
    }
    return peak;
}

double LoudnessMeter::windowEnergy(std::size_t subBlocks) const noexcept {
    const std::size_t count = std::min(subBlocks, ringCount_);
    if (count < subBlocks) {
        return 0;
    }
    double energy = 0;
    for (std::size_t channel = 0; channel < 2; ++channel) {
        double sum = 0;
        for (std::size_t i = 0; i < count; ++i) {
            sum += weightedRing_[channel][(ringIndex_ + 30 - 1 - i) % 30];
        }
        energy += sum / static_cast<double>(count * subBlock_);
    }
    return energy;
}

double LoudnessMeter::integratedLoudness() const noexcept {
    std::uint64_t count = 0;
    double energy = 0;
    for (std::size_t bin = 0; bin < histogramBins; ++bin) {
        count += histogramCount_[bin];
        energy += histogramEnergy_[bin];
    }
    if (count == 0) {
        return silence;
    }
    const double relativeGate = energyToLufs(energy / static_cast<double>(count)) - 10;
    const auto first = static_cast<std::size_t>(
        std::clamp(std::ceil((relativeGate - histogramFloor) / histogramStep), 0.0,
                   static_cast<double>(histogramBins)));
    std::uint64_t gatedCount = 0;
    double gatedEnergy = 0;
    for (std::size_t bin = first; bin < histogramBins; ++bin) {
        gatedCount += histogramCount_[bin];
        gatedEnergy += histogramEnergy_[bin];
    }
    return gatedCount == 0 ? silence : energyToLufs(gatedEnergy / static_cast<double>(gatedCount));
}

void LoudnessMeter::completeSubBlock() noexcept {
    for (std::size_t channel = 0; channel < 2; ++channel) {
        weightedRing_[channel][ringIndex_] = subWeighted_[channel];
        plainRing_[channel][ringIndex_ % 4] = subPlain_[channel];
    }
    ringIndex_ = (ringIndex_ + 1) % 30;
    ringCount_ = std::min<std::size_t>(ringCount_ + 1, 30);
    subWeighted_ = {};
    subPlain_ = {};
    const double block = windowEnergy(4);
    if (ringCount_ >= 4) {
        const double loudness = energyToLufs(block);
        if (loudness > absoluteGate) {
            const auto bin =
                static_cast<std::size_t>(std::clamp((loudness - histogramFloor) / histogramStep,
                                                    0.0, static_cast<double>(histogramBins - 1)));
            ++histogramCount_[bin];
            histogramEnergy_[bin] += block;
        }
        published_[0].store(static_cast<float>(loudness), std::memory_order_relaxed);
    }
    if (ringCount_ >= 30) {
        published_[1].store(static_cast<float>(energyToLufs(windowEnergy(30))),
                            std::memory_order_relaxed);
    }
    published_[2].store(static_cast<float>(integratedLoudness()), std::memory_order_relaxed);
    published_[3].store(peak_ > 0 ? static_cast<float>(20 * std::log10(peak_)) : silence,
                        std::memory_order_relaxed);
    for (std::size_t channel = 0; channel < 2; ++channel) {
        double sum = 0;
        for (const double value : plainRing_[channel]) {
            sum += value;
        }
        const double mean = sum / static_cast<double>(4 * subBlock_);
        published_[4 + channel].store(mean > 0 ? static_cast<float>(10 * std::log10(mean))
                                               : silence,
                                      std::memory_order_relaxed);
    }
}

void LoudnessMeter::process(const float *left, const float *right, std::size_t frames) noexcept {
    const float *channels[2] = {left, right};
    for (std::size_t i = 0; i < frames; ++i) {
        for (std::size_t channel = 0; channel < 2; ++channel) {
            const float sample = channels[channel][i];
            const double weighted = highpassState_[channel].process(
                highpass_, shelfState_[channel].process(shelf_, sample));
            subWeighted_[channel] += weighted * weighted;
            subPlain_[channel] += static_cast<double>(sample) * sample;
            peak_ = std::max(peak_, static_cast<double>(truePeakSample(channel, sample)));
        }
        historyIndex_ = (historyIndex_ + 1) % tapsPerPhase;
        if (++subBlockFill_ == subBlock_) {
            subBlockFill_ = 0;
            completeSubBlock();
        }
    }
}

LoudnessReading LoudnessMeter::reading() const noexcept {
    return {published_[0].load(std::memory_order_relaxed),
            published_[1].load(std::memory_order_relaxed),
            published_[2].load(std::memory_order_relaxed),
            published_[3].load(std::memory_order_relaxed),
            published_[4].load(std::memory_order_relaxed),
            published_[5].load(std::memory_order_relaxed)};
}
}
