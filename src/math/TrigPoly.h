#pragma once

#include "SimdVec.h"

#include <bit>
#include <concepts>
#include <cstdint>
#include <limits>
#include <numbers>
#include <type_traits>

namespace prism::math {
template <class T> struct SinCos {
    T sin;
    T cos;
};

namespace detail {
template <class V> [[nodiscard]] constexpr V sinePolynomial(V s) noexcept {
    return V(0.9999999999788490) +
           s * (V(-0.1666666660882607) +
                s * (V(0.0083333307205577) +
                     s * (V(-1.984083282326196e-4) +
                          s * (V(2.752397107463265e-6) + s * V(-2.386834652103103e-8)))));
}

template <class V> [[nodiscard]] constexpr V cosinePolynomial(V s) noexcept {
    return V(0.9999999999966450) +
           s * (V(-0.4999999999040934) +
                s * (V(0.0416666661919898) +
                     s * (V(-0.0013888879703277) +
                          s * (V(2.480071365561451e-5) +
                               s * (V(-2.751356111645714e-7) + s * V(1.976441829958418e-9))))));
}

template <class V> [[nodiscard]] constexpr V acosPolynomial(V u) noexcept {
    return V(1.5707963050) +
           u * (V(-0.2145988016) +
                u * (V(0.0889789874) +
                     u * (V(-0.0501743046) +
                          u * (V(0.0308918810) +
                               u * (V(-0.0170881256) +
                                    u * (V(0.0066700901) + u * V(-0.0012624911)))))));
}

template <std::floating_point T>
using UnsignedBits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;

template <std::floating_point T> [[nodiscard]] constexpr T flipSign(T value, bool flip) noexcept {
    using U = UnsignedBits<T>;
    return std::bit_cast<T>(std::bit_cast<U>(value) ^ (U(flip) << (sizeof(T) * 8 - 1)));
}

template <std::floating_point T> [[nodiscard]] constexpr T rootUnit(T value) noexcept {
    if (value == 0) {
        return 0;
    }
    using U = UnsignedBits<T>;
    constexpr U bias = sizeof(T) == 4 ? U(0x1fc00000U) : U(0x1ff8000000000000ULL);
    T root = std::bit_cast<T>((std::bit_cast<U>(value) >> 1) + bias);
    for (int i = 0; i < 7; ++i) {
        root = T(0.5) * (root + value / root);
    }
    return root;
}

template <std::floating_point T> struct ReducedAngle {
    T remainder;
    int multiple;
};

template <std::floating_point T> [[nodiscard]] constexpr ReducedAngle<T> reduceAngle(T x) noexcept {
    if (x == 0) {
        return {x, 0};
    }
    constexpr T bound = T(2000) * std::numbers::pi_v<T>;
    if (!(x >= -bound && x <= bound)) {
        return {std::numeric_limits<T>::quiet_NaN(), 0};
    }
    const T quotient = x * T(1 / std::numbers::pi);
    const int k = static_cast<int>(quotient + (x < 0 ? T(-0.5) : T(0.5)));
    const T r = (x - T(k) * T(0x1.922p+1)) - T(k) * T(-0x1.2aeef4p-17);
    return {r, k};
}
}

template <std::floating_point T>
    requires(sizeof(T) == 4 || sizeof(T) == 8)
[[nodiscard]] constexpr SinCos<T> sinCos(T x) noexcept {
    const auto reduced = detail::reduceAngle(x);
    const T s = reduced.remainder * reduced.remainder;
    const bool flip = (reduced.multiple & 1) != 0;
    return {detail::flipSign(reduced.remainder * detail::sinePolynomial(s), flip),
            detail::flipSign(detail::cosinePolynomial(s), flip)};
}

template <std::floating_point T>
    requires(sizeof(T) == 4 || sizeof(T) == 8)
[[nodiscard]] constexpr T acos(T x) noexcept {
    if (!(x >= -1 && x <= 1)) {
        return std::numeric_limits<T>::quiet_NaN();
    }
    const T u = x < 0 ? -x : x;
    const T result = detail::rootUnit(T(1) - u) * detail::acosPolynomial(u);
    return x < 0 ? std::numbers::pi_v<T> - result : result;
}

template <std::floating_point T, std::size_t N>
[[nodiscard]] constexpr SinCos<SimdVec<T, N>> sinCos(SimdVec<T, N> x) noexcept {
    using V = SimdVec<T, N>;
    using U = detail::UnsignedBits<T>;
    V r;
    SimdVec<U, N> signs;
    for (std::size_t i = 0; i < N; ++i) {
        const auto reduced = detail::reduceAngle(x[i]);
        r[i] = reduced.remainder;
        signs[i] = U(reduced.multiple & 1) << (sizeof(T) * 8 - 1);
    }
    const V s = r * r;
    const V sine = r * detail::sinePolynomial(s);
    const V cosine = detail::cosinePolynomial(s);
    return {(sine.template bitsAs<U>() ^ signs).template bitsAs<T>(),
            (cosine.template bitsAs<U>() ^ signs).template bitsAs<T>()};
}

template <std::floating_point T, std::size_t N>
[[nodiscard]] constexpr SimdVec<T, N> acos(SimdVec<T, N> x) noexcept {
    using V = SimdVec<T, N>;
    V u;
    V roots;
    for (std::size_t i = 0; i < N; ++i) {
        u[i] = x[i] < 0 ? -x[i] : x[i];
        roots[i] = u[i] <= 1 ? detail::rootUnit(T(1) - u[i]) : std::numeric_limits<T>::quiet_NaN();
    }
    V result = roots * detail::acosPolynomial(u);
    for (std::size_t i = 0; i < N; ++i) {
        result[i] = x[i] < 0 ? std::numbers::pi_v<T> - result[i] : result[i];
    }
    return result;
}
}
