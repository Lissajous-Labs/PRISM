#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prism::host {
struct ParameterSpec {
    std::string id;
    std::string name;
    float minimum = 0;
    float maximum = 1;
    float defaultValue = 0;
    float step = 0;
    std::string unit;
};

class HostParameterSink {
  public:
    virtual ~HostParameterSink() = default;
    virtual void beginGesture(std::uint32_t index) = 0;
    virtual void setValueNotifyingHost(std::uint32_t index, float normalized) = 0;
    virtual void endGesture(std::uint32_t index) = 0;
};

class ParameterMirror {
  public:
    explicit ParameterMirror(std::vector<ParameterSpec> specs);

    void hostSetNormalized(std::uint32_t index, float normalized) noexcept;
    [[nodiscard]] float normalized(std::uint32_t index) const noexcept {
        return values_[index].load(std::memory_order_acquire);
    }
    [[nodiscard]] float plain(std::uint32_t index) const noexcept {
        return toPlain(index, normalized(index));
    }
    [[nodiscard]] float toPlain(std::uint32_t index, float normalized) const noexcept;
    [[nodiscard]] float toNormalized(std::uint32_t index, float plain) const noexcept;
    [[nodiscard]] std::string format(std::uint32_t index) const;

    void beginEdit(std::uint32_t index, HostParameterSink &sink);
    void edit(std::uint32_t index, float normalized, HostParameterSink &sink);
    void endEdit(std::uint32_t index, HostParameterSink &sink);
    [[nodiscard]] bool editing(std::uint32_t index) const noexcept {
        return gestures_[index].load(std::memory_order_acquire);
    }

    template <class Visitor> std::size_t consumeChanges(Visitor &&visitor) {
        std::size_t count = 0;
        for (std::size_t w = 0; w < dirty_.size(); ++w) {
            for (std::uint64_t bits = dirty_[w].exchange(0, std::memory_order_acq_rel); bits != 0;
                 bits &= bits - 1) {
                const auto index = static_cast<std::uint32_t>(
                    w * 64 + static_cast<std::size_t>(std::countr_zero(bits)));
                visitor(index, normalized(index));
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::optional<std::uint32_t> indexOf(std::string_view id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return specs_.size(); }
    [[nodiscard]] const ParameterSpec &spec(std::uint32_t index) const noexcept {
        return specs_[index];
    }

  private:
    std::vector<ParameterSpec> specs_;
    std::unique_ptr<std::atomic<float>[]> values_;
    std::unique_ptr<std::atomic<bool>[]> gestures_;
    std::vector<std::atomic<std::uint64_t>> dirty_;

    void markDirty(std::uint32_t index) noexcept;
    [[nodiscard]] float quantize(std::uint32_t index, float normalized) const noexcept;
};

class MidiLearnMapper {
  public:
    static constexpr std::int32_t unbound = -1;

    MidiLearnMapper() {
        for (auto &slot : bindings_) {
            slot.store(unbound, std::memory_order_relaxed);
        }
    }

    void arm(std::uint32_t parameter) noexcept {
        armed_.store(static_cast<std::int32_t>(parameter), std::memory_order_release);
    }
    void unbind(std::uint8_t channel, std::uint8_t controller) noexcept {
        bindings_[slot(channel, controller)].store(unbound, std::memory_order_release);
    }
    [[nodiscard]] std::int32_t binding(std::uint8_t channel,
                                       std::uint8_t controller) const noexcept {
        return bindings_[slot(channel, controller)].load(std::memory_order_acquire);
    }

    bool handleController(std::uint8_t channel, std::uint8_t controller, std::uint8_t value,
                          ParameterMirror &mirror) noexcept {
        const std::size_t index = slot(channel, controller);
        const std::int32_t armed = armed_.exchange(unbound, std::memory_order_acq_rel);
        if (armed != unbound) {
            bindings_[index].store(armed, std::memory_order_release);
        }
        const std::int32_t parameter = bindings_[index].load(std::memory_order_acquire);
        if (parameter == unbound) {
            return false;
        }
        mirror.hostSetNormalized(static_cast<std::uint32_t>(parameter),
                                 static_cast<float>(value & 127) / 127.0f);
        return true;
    }

  private:
    std::array<std::atomic<std::int32_t>, 16 * 128> bindings_{};
    std::atomic<std::int32_t> armed_{unbound};

    static constexpr std::size_t slot(std::uint8_t channel, std::uint8_t controller) noexcept {
        return static_cast<std::size_t>(channel & 15) * 128 + (controller & 127);
    }
};

struct PlayheadState {
    double bpm = 120;
    double ppq = 0;
    std::int32_t numerator = 4;
    std::int32_t denominator = 4;
    bool playing = false;
    bool recording = false;

    friend bool operator==(const PlayheadState &, const PlayheadState &) = default;
};

class PlayheadSnapshot {
  public:
    void publish(const PlayheadState &state) noexcept {
        const std::uint32_t sequence = sequence_.load(std::memory_order_relaxed);
        sequence_.store(sequence + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        bpm_.store(std::bit_cast<std::uint64_t>(state.bpm), std::memory_order_relaxed);
        ppq_.store(std::bit_cast<std::uint64_t>(state.ppq), std::memory_order_relaxed);
        signature_.store(
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.numerator)) << 32) |
                static_cast<std::uint32_t>(state.denominator),
            std::memory_order_relaxed);
        flags_.store((state.playing ? 1U : 0U) | (state.recording ? 2U : 0U),
                     std::memory_order_relaxed);
        sequence_.store(sequence + 2, std::memory_order_release);
    }

    [[nodiscard]] PlayheadState read() const noexcept {
        for (;;) {
            const std::uint32_t before = sequence_.load(std::memory_order_acquire);
            if ((before & 1) != 0) {
                continue;
            }
            PlayheadState state;
            state.bpm = std::bit_cast<double>(bpm_.load(std::memory_order_relaxed));
            state.ppq = std::bit_cast<double>(ppq_.load(std::memory_order_relaxed));
            const std::uint64_t signature = signature_.load(std::memory_order_relaxed);
            state.numerator = static_cast<std::int32_t>(signature >> 32);
            state.denominator = static_cast<std::int32_t>(signature & 0xFFFFFFFF);
            const std::uint32_t flags = flags_.load(std::memory_order_relaxed);
            state.playing = (flags & 1) != 0;
            state.recording = (flags & 2) != 0;
            std::atomic_thread_fence(std::memory_order_acquire);
            if (sequence_.load(std::memory_order_relaxed) == before) {
                return state;
            }
        }
    }

  private:
    std::atomic<std::uint32_t> sequence_{0};
    std::atomic<std::uint64_t> bpm_{std::bit_cast<std::uint64_t>(120.0)};
    std::atomic<std::uint64_t> ppq_{0};
    std::atomic<std::uint64_t> signature_{(std::uint64_t{4} << 32) | 4};
    std::atomic<std::uint32_t> flags_{0};
};

[[nodiscard]] std::vector<std::uint8_t> saveState(const ParameterMirror &mirror);
[[nodiscard]] bool loadState(std::span<const std::uint8_t> bytes, ParameterMirror &mirror);
}
