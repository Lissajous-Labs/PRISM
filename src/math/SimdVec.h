#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#if defined(__x86_64__)
#include <emmintrin.h>
#endif

namespace prism::math {
template <class T>
concept VectorScalar = std::floating_point<T> || std::unsigned_integral<T>;

namespace detail {
template <VectorScalar T, std::size_t N> struct NativeVector {
    typedef T Type __attribute__((vector_size(sizeof(T) * N)));
};
}

template <VectorScalar T, std::size_t N>
    requires(N > 0 && std::has_single_bit(N) && sizeof(T) * N <= 32)
class SimdVec {
  public:
    using Scalar = T;
    static constexpr std::size_t size = N;

    constexpr SimdVec() noexcept = default;
    constexpr explicit SimdVec(std::array<T, N> lanes) noexcept : lanes_(lanes) {}
    constexpr explicit SimdVec(T value) noexcept { lanes_.fill(value); }

    [[nodiscard]] constexpr T operator[](std::size_t index) const noexcept { return lanes_[index]; }
    constexpr T &operator[](std::size_t index) noexcept { return lanes_[index]; }
    [[nodiscard]] constexpr const std::array<T, N> &lanes() const noexcept { return lanes_; }

    [[nodiscard]] friend constexpr SimdVec operator+(SimdVec left, SimdVec right) noexcept {
        if consteval {
            for (std::size_t i = 0; i < N; ++i) {
                left.lanes_[i] += right.lanes_[i];
            }
            return left;
        } else {
            return fromNative(left.native() + right.native());
        }
    }
    [[nodiscard]] friend constexpr SimdVec operator-(SimdVec left, SimdVec right) noexcept {
        if consteval {
            for (std::size_t i = 0; i < N; ++i) {
                left.lanes_[i] -= right.lanes_[i];
            }
            return left;
        } else {
            return fromNative(left.native() - right.native());
        }
    }
    [[nodiscard]] friend constexpr SimdVec operator*(SimdVec left, SimdVec right) noexcept {
        if consteval {
            for (std::size_t i = 0; i < N; ++i) {
                left.lanes_[i] *= right.lanes_[i];
            }
            return left;
        } else {
            return fromNative(left.native() * right.native());
        }
    }
    [[nodiscard]] friend constexpr SimdVec operator/(SimdVec left, SimdVec right) noexcept
        requires std::floating_point<T>
    {
        if consteval {
            for (std::size_t i = 0; i < N; ++i) {
                left.lanes_[i] /= right.lanes_[i];
            }
            return left;
        } else {
            return fromNative(left.native() / right.native());
        }
    }
    [[nodiscard]] friend constexpr SimdVec operator^(SimdVec left, SimdVec right) noexcept
        requires std::unsigned_integral<T>
    {
        if consteval {
            for (std::size_t i = 0; i < N; ++i) {
                left.lanes_[i] ^= right.lanes_[i];
            }
            return left;
        } else {
            return fromNative(left.native() ^ right.native());
        }
    }
    [[nodiscard]] friend constexpr SimdVec operator&(SimdVec left, SimdVec right) noexcept
        requires std::unsigned_integral<T>
    {
        if consteval {
            for (std::size_t i = 0; i < N; ++i) {
                left.lanes_[i] &= right.lanes_[i];
            }
            return left;
        } else {
            return fromNative(left.native() & right.native());
        }
    }
    [[nodiscard]] friend constexpr SimdVec operator|(SimdVec left, SimdVec right) noexcept
        requires std::unsigned_integral<T>
    {
        if consteval {
            for (std::size_t i = 0; i < N; ++i) {
                left.lanes_[i] |= right.lanes_[i];
            }
            return left;
        } else {
            return fromNative(left.native() | right.native());
        }
    }

    template <std::size_t... Indices>
        requires(sizeof...(Indices) == N && ((Indices < N) && ...))
    [[nodiscard]] constexpr SimdVec shuffle() const noexcept {
        if consteval {
            return SimdVec(std::array<T, N>{lanes_[Indices]...});
        } else {
            return fromNative(__builtin_shufflevector(native(), native(), Indices...));
        }
    }

    [[nodiscard]] constexpr T sum() const noexcept {
        auto values = *this;
        reduceSum<N>(values);
        return values[0];
    }

    [[nodiscard]] constexpr bool isZero() const noexcept {
        if consteval {
            for (const T lane : lanes_) {
                if (lane != 0) {
                    return false;
                }
            }
            return true;
        } else {
            const auto mask = native() != Native{};
            return std::bit_cast<std::array<T, N>>(mask) == std::array<T, N>{};
        }
    }

    template <VectorScalar U>
        requires(sizeof(U) == sizeof(T))
    [[nodiscard]] constexpr SimdVec<U, N> bitsAs() const noexcept {
        return SimdVec<U, N>(std::bit_cast<std::array<U, N>>(lanes_));
    }

    void streamStore(T *destination) const noexcept
        requires(sizeof(T) * N >= 16)
    {
#if defined(__clang__)
        __builtin_nontemporal_store(native(), reinterpret_cast<Native *>(destination));
#elif defined(__x86_64__)
        const auto blocks =
            std::bit_cast<std::array<std::array<std::uint64_t, 2>, sizeof(T) * N / 16>>(lanes_);
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            const auto block = std::bit_cast<__m128i>(blocks[i]);
            _mm_stream_si128(reinterpret_cast<__m128i *>(destination) + i, block);
        }
#elif defined(__aarch64__)
        const auto words = std::bit_cast<std::array<std::uint64_t, sizeof(T) * N / 8>>(lanes_);
        for (std::size_t i = 0; i < words.size(); i += 2) {
            auto *output = reinterpret_cast<std::uint64_t *>(destination) + i;
            asm volatile("stnp %1, %2, [%0]"
                         :
                         : "r"(output), "r"(words[i]), "r"(words[i + 1])
                         : "memory");
        }
#else
        static_assert(sizeof(T) == 0, "Streaming stores require a supported compiler target");
#endif
    }

    static void finishStreamStores() noexcept {
#if defined(__x86_64__)
        _mm_sfence();
#elif defined(__aarch64__)
        asm volatile("dmb ishst" : : : "memory");
#endif
    }

  private:
    using Native = typename detail::NativeVector<T, N>::Type;
    std::array<T, N> lanes_{};

    [[nodiscard]] Native native() const noexcept { return std::bit_cast<Native>(lanes_); }
    [[nodiscard]] static SimdVec fromNative(Native value) noexcept {
        return SimdVec(std::bit_cast<std::array<T, N>>(value));
    }

    template <std::size_t Width> static constexpr void reduceSum(SimdVec &values) noexcept {
        if constexpr (Width > 1) {
            for (std::size_t i = 0; i < Width / 2; ++i) {
                values[i] = values[2 * i] + values[2 * i + 1];
            }
            reduceSum<Width / 2>(values);
        }
    }
};

template <std::size_t N>
[[nodiscard]] constexpr std::uint32_t bmask(SimdVec<std::uint8_t, N> bytes) noexcept {
    std::uint32_t mask = 0;
    for (std::size_t i = 0; i < N; ++i) {
        mask |= static_cast<std::uint32_t>(bytes[i] >> 7) << i;
    }
    return mask;
}
}
