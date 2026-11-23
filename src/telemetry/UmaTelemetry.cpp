#include "telemetry/UmaTelemetry.h"

#include <cmath>

namespace prism::telemetry {

UmaWriter::UmaWriter(UmaLayout &layout, double sampleRate)
    : layout_(layout), loudness_(sampleRate), taps_(designHalfBand()),
      stages_{HalfBandDecimator(taps_), HalfBandDecimator(taps_), HalfBandDecimator(taps_)} {}

void UmaWriter::push(std::uint32_t band, float value) noexcept {
    layout_.bands[band][bandFrames_[band] & umaMask] = value;
    ++bandFrames_[band];
}

void UmaWriter::write(std::span<const float> left, std::span<const float> right) noexcept {
    const std::size_t frames = left.size() < right.size() ? left.size() : right.size();
    for (std::size_t n = 0; n < frames; ++n) {
        const std::uint32_t at = static_cast<std::uint32_t>(frames_ & umaMask);
        layout_.scope[0][at] = left[n];
        layout_.scope[1][at] = right[n];
        ++frames_;

        double value = 0.5 * (static_cast<double>(left[n]) + right[n]);
        push(umaBands - 1, static_cast<float>(value));
        for (std::uint32_t stage = 0; stage < stages_.size(); ++stage) {
            if (!stages_[stage].push(value, value)) {
                break;
            }
            push(umaBands - 2 - stage, static_cast<float>(value));
        }
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
