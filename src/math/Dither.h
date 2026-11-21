#pragma once

#include "ColorSpace.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace prism::math {
[[nodiscard]] inline double interleavedGradientNoise(double x, double y) noexcept {
    const double inner = 0.06711056 * x + 0.00583715 * y;
    const double scaled = 52.9829189 * (inner - std::floor(inner));
    return scaled - std::floor(scaled);
}

[[nodiscard]] inline double triangularNoise(double uniform) noexcept {
    return uniform < 0.5 ? std::sqrt(2 * uniform) - 1 : 1 - std::sqrt(2 * (1 - uniform));
}

[[nodiscard]] inline std::uint8_t quantizeDithered(double encodedLevel, double uniform) noexcept {
    const double level = std::floor(encodedLevel + 0.5 + triangularNoise(uniform));
    return static_cast<std::uint8_t>(std::clamp(level, 0.0, 255.0));
}

[[nodiscard]] inline std::uint8_t quantizeLinear(double linear, double uniform) noexcept {
    return quantizeDithered(255 * srgbEncode(linear), uniform);
}
}
