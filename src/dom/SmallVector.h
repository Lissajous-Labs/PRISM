#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>

namespace prism::dom {
template <std::size_t N>
using SmallSize =
    std::conditional_t<N <= std::numeric_limits<std::uint8_t>::max(), std::uint8_t,
                       std::conditional_t<N <= std::numeric_limits<std::uint16_t>::max(),
                                          std::uint16_t, std::uint32_t>>;

template <class T, std::size_t N>
    requires(N > 0 && std::is_trivially_copyable_v<T>)
class SmallVector {
  public:
    using size_type = SmallSize<N>;

    constexpr SmallVector() noexcept = default;

    [[nodiscard]] constexpr bool push_back(const T &value) noexcept {
        if (size_ == N) {
            return false;
        }
        items_[size_++] = value;
        return true;
    }

    constexpr void erase(std::size_t index) noexcept {
        for (std::size_t i = index + 1; i < size_; ++i) {
            items_[i - 1] = items_[i];
        }
        --size_;
    }

    constexpr void clear() noexcept { size_ = 0; }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return N; }
    [[nodiscard]] constexpr T &operator[](std::size_t i) noexcept { return items_[i]; }
    [[nodiscard]] constexpr const T &operator[](std::size_t i) const noexcept { return items_[i]; }
    [[nodiscard]] constexpr T *begin() noexcept { return items_.data(); }
    [[nodiscard]] constexpr T *end() noexcept { return items_.data() + size_; }
    [[nodiscard]] constexpr const T *begin() const noexcept { return items_.data(); }
    [[nodiscard]] constexpr const T *end() const noexcept { return items_.data() + size_; }
    [[nodiscard]] constexpr std::span<const T> view() const noexcept {
        return {items_.data(), size_};
    }

    friend constexpr bool operator==(const SmallVector &a, const SmallVector &b) noexcept {
        if (a.size_ != b.size_) {
            return false;
        }
        for (std::size_t i = 0; i < a.size_; ++i) {
            if (!(a.items_[i] == b.items_[i])) {
                return false;
            }
        }
        return true;
    }

  private:
    std::array<T, N> items_{};
    size_type size_ = 0;
};
}
