#include "telemetry/UmaTelemetry.h"

#include <cmath>
#include <numbers>

namespace prism::telemetry {
namespace {
constexpr double kaiserBeta = 10.0;

double besselI0(double x) noexcept {
    double sum = 1;
    double term = 1;
    for (int k = 1; k < 64; ++k) {
        term *= (x / (2 * k)) * (x / (2 * k));
        sum += term;
    }
    return sum;
}
}

std::array<double, halfBandTaps> designHalfBand() noexcept {
    std::array<double, halfBandTaps> taps{};
    constexpr int centre = static_cast<int>(halfBandTaps / 2);
    const double norm = besselI0(kaiserBeta);
    double sum = 0;

    for (int n = 0; n < static_cast<int>(halfBandTaps); ++n) {
        const int k = n - centre;
        const double ratio = static_cast<double>(k) / centre;
        const double window = besselI0(kaiserBeta * std::sqrt(1 - ratio * ratio)) / norm;
        const double ideal = k == 0 ? 0.5
                                    : std::sin(std::numbers::pi * k / 2) /
                                          (std::numbers::pi * static_cast<double>(k));
        taps[static_cast<std::size_t>(n)] = k != 0 && k % 2 == 0 ? 0.0 : ideal * window;
        sum += taps[static_cast<std::size_t>(n)];
    }

    for (double &tap : taps) {
        tap /= sum;
    }
    return taps;
}

UmaWriter::UmaWriter(UmaLayout &layout, double sampleRate)
    : layout_(layout), loudness_(sampleRate), taps_(designHalfBand()) {}

void UmaWriter::push(std::uint32_t band, float value) noexcept {
    layout_.bands[band][bandFrames_[band] & umaMask] = value;
    ++bandFrames_[band];
}

void UmaWriter::decimate(std::uint32_t stage, double value) noexcept {
    Stage &s = stages_[stage];
    s.history[s.position] = value;
    s.history[s.position + halfBandTaps] = value;
    s.position = s.position + 1 == halfBandTaps ? 0 : s.position + 1;
    s.odd = !s.odd;
    if (s.odd) {
        return;
    }

    const double *window = s.history.data() + s.position;
    double sum = taps_[halfBandTaps / 2] * window[halfBandTaps / 2];
    for (std::uint32_t k = 0; k < halfBandTaps; k += 2) {
        sum += taps_[k] * window[k];
    }

    const std::uint32_t band = umaBands - 2 - stage;
    push(band, static_cast<float>(sum));
    if (stage + 1 < stages_.size()) {
        decimate(stage + 1, sum);
    }
}

void UmaWriter::write(std::span<const float> left, std::span<const float> right) noexcept {
    const std::size_t frames = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t n = 0; n < frames; ++n) {
        const std::uint32_t at = static_cast<std::uint32_t>(frames_ & umaMask);
        layout_.scope[0][at] = left[n];
        layout_.scope[1][at] = right[n];
        ++frames_;

        const double mono = 0.5 * (static_cast<double>(left[n]) + right[n]);
        push(umaBands - 1, static_cast<float>(mono));
        decimate(0, mono);
    }
    loudness_.process(left.data(), right.data(), frames);

    const LoudnessReading reading = loudness_.reading();
    const std::uint64_t sequence = layout_.publishedSeq.load(std::memory_order_relaxed) + 1;
    UmaSlot &slot = layout_.ring[sequence % umaSlots];
    std::atomic_thread_fence(std::memory_order_release);
    slot.frames.store(frames_, std::memory_order_relaxed);
    for (std::uint32_t b = 0; b < umaBands; ++b) {
        slot.bandFrames[b].store(bandFrames_[b], std::memory_order_relaxed);
    }
    slot.momentary.store(reading.momentary, std::memory_order_relaxed);
    slot.shortTerm.store(reading.shortTerm, std::memory_order_relaxed);
    slot.integrated.store(reading.integrated, std::memory_order_relaxed);
    slot.truePeak.store(reading.truePeak, std::memory_order_relaxed);
    layout_.publishedSeq.store(sequence, std::memory_order_release);
}
}
