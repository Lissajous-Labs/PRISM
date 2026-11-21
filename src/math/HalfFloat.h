#pragma once

#include <bit>
#include <cstdint>

namespace prism::math {
[[nodiscard]] constexpr std::uint16_t floatToHalf(float value) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const auto sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
    const auto exponent = static_cast<int>((bits >> 23) & 0xFFu);
    const auto mantissa = bits & 0x7FFFFFu;
    if (exponent == 255) {
        return static_cast<std::uint16_t>(sign | 0x7C00u | (mantissa == 0 ? 0u : 0x200u));
    }
    const int halfExponent = exponent - 112;
    if (halfExponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7C00u);
    }
    if (halfExponent <= 0) {
        if (halfExponent < -10) {
            return sign;
        }
        const std::uint32_t full = mantissa | 0x800000u;
        const auto shift = static_cast<unsigned>(14 - halfExponent);
        const auto base = full >> shift;
        const auto remainder = full & ((1u << shift) - 1u);
        const auto halfway = 1u << (shift - 1u);
        return static_cast<std::uint16_t>(
            sign | (base + (remainder > halfway || (remainder == halfway && (base & 1u)))));
    }
    const auto base = (static_cast<std::uint32_t>(halfExponent) << 10) | (mantissa >> 13);
    const auto remainder = mantissa & 0x1FFFu;
    return static_cast<std::uint16_t>(
        sign | (base + (remainder > 0x1000u || (remainder == 0x1000u && (base & 1u)))));
}
[[nodiscard]] constexpr float halfToFloat(std::uint16_t value) noexcept {
    const auto sign = static_cast<std::uint32_t>(value & 0x8000u) << 16;
    const auto exponent = (value >> 10) & 31u;
    const auto mantissa = value & 1023u;
    if (exponent == 0) {
        const float magnitude = static_cast<float>(mantissa) * 0x1p-24f;
        return sign != 0 ? -magnitude : magnitude;
    }
    if (exponent == 31) {
        return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13));
    }
    return std::bit_cast<float>(sign | ((exponent + 112u) << 23) | (mantissa << 13));
}
[[nodiscard]] constexpr float halfRound(float value) noexcept {
    return halfToFloat(floatToHalf(value));
}
}
