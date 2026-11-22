#pragma once

#include <algorithm>
#include <cmath>

namespace prism::math {
inline constexpr float stemGamma = 0.065f;
inline constexpr float stemDarkBoost = 0.085f;
inline constexpr float stemGammaScale = 18.0f;
inline constexpr float stemDarkScale = 14.0f;
inline constexpr float darkBackground = 0.05f;
inline constexpr float polarityPivot = 0.18f;

[[nodiscard]] inline float polarityStemBias(float ppem, float background,
                                            float foreground) noexcept {
    const float polarity = background > foreground ? 1.0f : background < foreground ? -1.0f : 0.0f;
    const float dark =
        background < darkBackground ? stemDarkBoost * std::exp(-ppem / stemDarkScale) : 0.0f;
    return polarity * stemGamma * std::exp(-ppem / stemGammaScale) + dark;
}

[[nodiscard]] inline float shiftedCoverage(float coverage, float bias) noexcept {
    return coverage > 0 ? std::clamp(coverage + bias, 0.0f, 1.0f) : 0.0f;
}

[[nodiscard]] inline float resolveBackground(float background, float foreground) noexcept {
    if (background >= 0) {
        return background;
    }
    return foreground > polarityPivot ? 0.0f : 1.0f;
}

[[nodiscard]] inline float foregroundLuminance(float r, float g, float b, float a) noexcept {
    return a > 0 ? (0.2126f * r + 0.7152f * g + 0.0722f * b) / a : 0.0f;
}
}
