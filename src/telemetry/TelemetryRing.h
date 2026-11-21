#pragma once

#include "core/Realtime.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace prism::telemetry {
inline constexpr std::size_t cacheLine = 64;

template <class T, std::size_t N>
    requires(std::has_single_bit(N) && std::is_trivially_copyable_v<T>)
class TelemetryRing {
  public:
    bool push(const T *items, std::size_t count) noexcept PRISM_NONBLOCKING {
        const std::size_t write = write_.load(std::memory_order_relaxed);
        const std::size_t read = read_.load(std::memory_order_acquire);
        if (count > N - (write - read)) {
            dropped_.fetch_add(count, std::memory_order_relaxed);
            return false;
        }
        const std::size_t offset = write & (N - 1);
        const std::size_t first = count < N - offset ? count : N - offset;
        std::memcpy(slots_.data() + offset, items, first * sizeof(T));
        std::memcpy(slots_.data(), items + first, (count - first) * sizeof(T));
        write_.store(write + count, std::memory_order_release);
        return true;
    }

    std::size_t pop(T *out, std::size_t capacity) noexcept PRISM_NONBLOCKING {
        const std::size_t read = read_.load(std::memory_order_relaxed);
        const std::size_t write = write_.load(std::memory_order_acquire);
        const std::size_t count = write - read < capacity ? write - read : capacity;
        const std::size_t offset = read & (N - 1);
        const std::size_t first = count < N - offset ? count : N - offset;
        std::memcpy(out, slots_.data() + offset, first * sizeof(T));
        std::memcpy(out + first, slots_.data(), (count - first) * sizeof(T));
        read_.store(read + count, std::memory_order_release);
        return count;
    }

    [[nodiscard]] std::size_t available() const noexcept {
        return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::size_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

  private:
    alignas(cacheLine) std::atomic<std::size_t> write_{0};
    alignas(cacheLine) std::atomic<std::size_t> read_{0};
    alignas(cacheLine) std::atomic<std::size_t> dropped_{0};
    alignas(cacheLine) std::array<T, N> slots_{};
};
}
