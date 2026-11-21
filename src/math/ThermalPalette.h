#pragma once

#include "math/ColorSpace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace prism::math {
[[nodiscard]] inline double thermalSmoothstep(double low, double high, double x) noexcept {
    const double t = std::clamp((x - low) / (high - low), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

[[nodiscard]] inline Oklch thermalOklch(double energy) noexcept {
    const double z = std::clamp(energy, 0.0, 1.0);
    return {0.16 + 0.805 * std::pow(z, 0.72),
            0.168 * std::pow(4 * z * (1 - 0.72 * z), 0.65) * (1 - 0.86 * z * z * z * z),
            (34 + 48 * std::pow(thermalSmoothstep(0.05, 0.98, z), 1.15)) * std::numbers::pi / 180};
}

[[nodiscard]] inline LinearRgb thermalEmission(double energy, double overdrive = 1.15) noexcept {
    const double z = std::clamp(energy, 0.0, 1.0);
    const auto base = fromOklab(fromOklch(thermalOklch(z)));
    const double gain = 1 + overdrive * std::pow(thermalSmoothstep(0.72, 1, z), 1.6);
    return {gain * std::clamp(base.r, 0.0, 1.0), gain * std::clamp(base.g, 0.0, 1.0),
            gain * std::clamp(base.b, 0.0, 1.0)};
}

[[nodiscard]] inline std::array<LinearRgb, 256> thermalPalette(double overdrive = 1.15) noexcept {
    std::array<LinearRgb, 256> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = thermalEmission(static_cast<double>(i) / 255, overdrive);
    }
    return result;
}
}
