#pragma once

#include "core/Realtime.h"
#include "telemetry/LoudnessMeter.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace prism::telemetry {
inline constexpr std::uint32_t umaSlots = 3;
inline constexpr std::uint32_t umaBands = 4;
inline constexpr std::uint32_t umaCapacity = 1u << 14;
inline constexpr std::uint32_t umaMask = umaCapacity - 1;
inline constexpr std::uint32_t halfBandTaps = 23;

struct UmaSlot {
    std::atomic<std::uint64_t> frames{0};
    std::array<std::atomic<std::uint64_t>, umaBands> bandFrames{};
    std::atomic<float> momentary{silence};
    std::atomic<float> shortTerm{silence};
    std::atomic<float> integrated{silence};
    std::atomic<float> truePeak{silence};
};

struct UmaSnapshot {
    std::uint64_t sequence = 0;
    std::uint64_t frames = 0;
    std::array<std::uint64_t, umaBands> bandFrames{};
    LoudnessReading loudness{silence, silence, silence, silence, silence, silence};
};

struct alignas(64) UmaLayout {
    alignas(64) std::atomic<std::uint64_t> publishedSeq{0};
    alignas(64) std::array<UmaSlot, umaSlots> ring{};
    alignas(64) std::array<std::array<float, umaCapacity>, 2> scope{};
    alignas(64) std::array<std::array<float, umaCapacity>, umaBands> bands{};
};

inline constexpr std::size_t umaScopeOffset = offsetof(UmaLayout, scope);
inline constexpr std::size_t umaBandOffset = offsetof(UmaLayout, bands);

[[nodiscard]] std::array<double, halfBandTaps> designHalfBand() noexcept;

[[nodiscard]] inline bool snapshot(const UmaLayout &layout, UmaSnapshot &out) noexcept {
    for (std::uint32_t attempt = 0; attempt < 8; ++attempt) {
        const std::uint64_t sequence = layout.publishedSeq.load(std::memory_order_acquire);
        if (sequence == 0) {
            return false;
        }

        const UmaSlot &slot = layout.ring[sequence % umaSlots];
        UmaSnapshot copy;
        copy.sequence = sequence;
        copy.frames = slot.frames.load(std::memory_order_relaxed);
        for (std::uint32_t b = 0; b < umaBands; ++b) {
            copy.bandFrames[b] = slot.bandFrames[b].load(std::memory_order_relaxed);
        }
        copy.loudness.momentary = slot.momentary.load(std::memory_order_relaxed);
        copy.loudness.shortTerm = slot.shortTerm.load(std::memory_order_relaxed);
        copy.loudness.integrated = slot.integrated.load(std::memory_order_relaxed);
        copy.loudness.truePeak = slot.truePeak.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);

        if (layout.publishedSeq.load(std::memory_order_relaxed) <= sequence + 1) {
            out = copy;
            return true;
        }
    }
    return false;
}

class UmaWriter {
  public:
    UmaWriter(UmaLayout &layout, double sampleRate);

    void write(std::span<const float> left,
               std::span<const float> right) noexcept PRISM_NONBLOCKING;

  private:
    struct Stage {
        std::array<double, 2 * halfBandTaps> history{};
        std::uint32_t position = 0;
        bool odd = false;
    };

    void push(std::uint32_t band, float value) noexcept;
    void decimate(std::uint32_t stage, double value) noexcept;

    UmaLayout &layout_;
    LoudnessMeter loudness_;
    std::array<double, halfBandTaps> taps_;
    std::array<Stage, umaBands - 1> stages_{};
    std::uint64_t frames_ = 0;
    std::array<std::uint64_t, umaBands> bandFrames_{};
};
}
