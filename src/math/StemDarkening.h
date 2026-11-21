#pragma once

#include <algorithm>
#include <cmath>

namespace prism::math {
[[nodiscard]] inline float stemDarkening(float coverage, float foregroundLuminance) noexcept {
    const float gamma = std::pow(std::clamp(foregroundLuminance, 0.0f, 1.0f), 1.0f / 2.2f);
    const float beta = 0.28f * (1.0f - 2.0f * gamma);
    return coverage * (1.0f + beta) / (1.0f + beta * coverage);
}

[[nodiscard]] inline float foregroundLuminance(float r, float g, float b, float a) noexcept {
    return a > 0 ? (0.2126f * r + 0.7152f * g + 0.0722f * b) / a : 0.0f;
}
}
