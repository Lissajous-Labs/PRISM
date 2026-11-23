#pragma once

#include "math/Vector2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace prism::render {
inline constexpr std::uint32_t slugHeader = 8;
inline constexpr std::uint32_t slugCurveFloats = 6;
inline constexpr std::uint32_t slugMaxBands = 16;

namespace slug {
[[nodiscard]] inline double saturate(double value) noexcept { return std::clamp(value, 0.0, 1.0); }

[[nodiscard]] inline std::uint32_t eligibility(double a, double b, double c) noexcept {
    const std::uint32_t shift = (a > 0 ? 2u : 0u) + (b > 0 ? 4u : 0u) + (c > 0 ? 8u : 0u);
    return (0x2E74u >> shift) & 3u;
}

inline void roots(double a, double b, double c, double &t1, double &t2) noexcept {
    if (std::abs(a) <= 1e-12 * (std::abs(b) + std::abs(c))) {
        t1 = t2 = std::abs(b) > 0 ? c * 0.5 / b : 0;
        return;
    }
    const double d = std::sqrt(std::max(b * b - a * c, 0.0));
    if (b >= 0) {
        const double q = b + d;
        t2 = q / a;
        t1 = q != 0 ? c / q : t2;
    } else {
        const double q = b - d;
        t1 = q / a;
        t2 = q != 0 ? c / q : t1;
    }
}
}

[[nodiscard]] inline double slugCoverage(std::span<const float> blob, math::Vector2 em,
                                         double pixelsPerEmX, double pixelsPerEmY) noexcept {
    using math::Vector2;
    using slug::saturate;
    if (blob.size() < slugHeader) {
        return 0;
    }
    const double x0 = blob[0];
    const double y0 = blob[1];
    const double x1 = blob[2];
    const double y1 = blob[3];
    const auto hBands = static_cast<std::uint32_t>(blob[5]);
    const auto vBands = static_cast<std::uint32_t>(blob[6]);
    const auto curveBase = static_cast<std::size_t>(blob[7]);
    const auto band = [](double value, double low, double high, std::uint32_t count) {
        const double t = (value - low) / (high - low);
        return static_cast<std::uint32_t>(
            std::clamp(static_cast<int>(std::floor(t * count)), 0, static_cast<int>(count) - 1));
    };
    const auto curve = [&](std::size_t index) {
        const float *c = blob.data() + curveBase + index * slugCurveFloats;
        return std::array<Vector2, 3>{Vector2{c[0] - em.x, c[1] - em.y},
                                      Vector2{c[2] - em.x, c[3] - em.y},
                                      Vector2{c[4] - em.x, c[5] - em.y}};
    };

    double horizontal = 0;
    double horizontalWeight = 0;
    const std::size_t hEntry = slugHeader + 2 * band(em.y, y0, y1, hBands);
    for (std::size_t k = 0; k < static_cast<std::size_t>(blob[hEntry + 1]); ++k) {
        const auto [p1, p2, p3] =
            curve(static_cast<std::size_t>(blob[static_cast<std::size_t>(blob[hEntry]) + k]));
        const std::uint32_t code = slug::eligibility(p1.y, p2.y, p3.y);
        if (code == 0) {
            continue;
        }
        const Vector2 a = p1 - p2 * 2 + p3;
        const Vector2 b = p1 - p2;
        double t1 = 0;
        double t2 = 0;
        slug::roots(a.y, b.y, p1.y, t1, t2);
        const double r1 = (a.x * t1 - b.x * 2) * t1 + p1.x;
        const double r2 = (a.x * t2 - b.x * 2) * t2 + p1.x;
        if ((code & 1u) != 0) {
            horizontal += saturate(r1 * pixelsPerEmX + 0.5);
            horizontalWeight =
                std::max(horizontalWeight, saturate(1 - std::abs(r1 * pixelsPerEmX) * 2));
        }
        if (code > 1) {
            horizontal -= saturate(r2 * pixelsPerEmX + 0.5);
            horizontalWeight =
                std::max(horizontalWeight, saturate(1 - std::abs(r2 * pixelsPerEmX) * 2));
        }
    }

    double vertical = 0;
    double verticalWeight = 0;
    const std::size_t vEntry = slugHeader + 2 * (hBands + band(em.x, x0, x1, vBands));
    for (std::size_t k = 0; k < static_cast<std::size_t>(blob[vEntry + 1]); ++k) {
        const auto [p1, p2, p3] =
            curve(static_cast<std::size_t>(blob[static_cast<std::size_t>(blob[vEntry]) + k]));
        const std::uint32_t code = slug::eligibility(p1.x, p2.x, p3.x);
        if (code == 0) {
            continue;
        }
        const Vector2 a = p1 - p2 * 2 + p3;
        const Vector2 b = p1 - p2;
        double t1 = 0;
        double t2 = 0;
        slug::roots(a.x, b.x, p1.x, t1, t2);
        const double r1 = (a.y * t1 - b.y * 2) * t1 + p1.y;
        const double r2 = (a.y * t2 - b.y * 2) * t2 + p1.y;
        if ((code & 1u) != 0) {
            vertical -= saturate(r1 * pixelsPerEmY + 0.5);
            verticalWeight =
                std::max(verticalWeight, saturate(1 - std::abs(r1 * pixelsPerEmY) * 2));
        }
        if (code > 1) {
            vertical += saturate(r2 * pixelsPerEmY + 0.5);
            verticalWeight =
                std::max(verticalWeight, saturate(1 - std::abs(r2 * pixelsPerEmY) * 2));
        }
    }

    const double weighted = std::abs(horizontal * horizontalWeight + vertical * verticalWeight) /
                            std::max(horizontalWeight + verticalWeight, 1.0 / 65536);
    return saturate(std::max(weighted, std::min(std::abs(horizontal), std::abs(vertical))));
}
}
