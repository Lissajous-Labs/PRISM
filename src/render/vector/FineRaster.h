#pragma once

#include "render/vector/PathTiler.h"

#include <cmath>
#include <cstdint>
#include <span>

namespace prism::render {
[[nodiscard]] inline float coverageAntiderivative(float u) noexcept {
    if (u <= 0) {
        return u;
    }
    if (u >= 1) {
        return 0.5f;
    }
    return u - 0.5f * u * u;
}

[[nodiscard]] inline float coverageWeight(float u) noexcept {
    return u <= 0 ? 1.0f : (u >= 1 ? 0.0f : 1.0f - u);
}

[[nodiscard]] inline float pieceContribution(PackedPiece packed, std::uint32_t column,
                                             std::uint32_t row) noexcept {
    const float x0 = dequantize(packed.x0);
    const float y0 = dequantize(packed.y0);
    const float x1 = dequantize(packed.x1);
    const float y1 = dequantize(packed.y1);
    const float top = static_cast<float>(row);
    const float ya = std::fmax(std::fmin(y0, y1), top);
    const float yb = std::fmin(std::fmax(y0, y1), top + 1.0f);
    if (!(yb > ya) || y0 == y1) {
        return 0.0f;
    }
    const float slope = (x1 - x0) / (y1 - y0);
    const float xa = x0 + (ya - y0) * slope - static_cast<float>(column);
    const float xb = x0 + (yb - y0) * slope - static_cast<float>(column);
    const float dy = (yb - ya) * (y1 > y0 ? 1.0f : -1.0f);
    const float average =
        std::fabs(xb - xa) > 1e-5f
            ? (coverageAntiderivative(xb) - coverageAntiderivative(xa)) / (xb - xa)
            : coverageWeight(0.5f * (xa + xb));
    return dy * average;
}

inline constexpr float windingScale = 1048576.0f;

[[nodiscard]] inline std::int32_t fixedContribution(PackedPiece packed, std::uint32_t column,
                                                    std::uint32_t row) noexcept {
    return static_cast<std::int32_t>(
        std::lrint(pieceContribution(packed, column, row) * windingScale));
}

[[nodiscard]] inline float resolveWinding(std::int32_t fixedSum) noexcept {
    return static_cast<float>((fixedSum + 128) >> 8) / 4096.0f;
}

[[nodiscard]] inline float windingToAlpha(float winding, FillRule rule) noexcept {
    const float magnitude = std::fabs(winding);
    if (rule == FillRule::nonZero) {
        return std::fmin(magnitude, 1.0f);
    }
    return 1.0f - std::fabs(std::fmod(magnitude, 2.0f) - 1.0f);
}

void rasterizeCoverage(const PathTiler &tiler, FillRule rule, std::uint32_t width,
                       std::uint32_t height, std::span<float> coverage);
}
