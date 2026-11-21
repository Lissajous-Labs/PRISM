#pragma once

#include "Vector2.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace prism::math {
inline constexpr double defaultFlattenTolerance = 0.125;

[[nodiscard]] inline std::uint32_t
quadraticSegments(Vector2 p0, Vector2 p1, Vector2 p2,
                  double tolerance = defaultFlattenTolerance) noexcept {
    const double deviation = length(p0 - p1 * 2 + p2);
    return std::max<std::uint32_t>(
        1, static_cast<std::uint32_t>(std::ceil(std::sqrt(deviation / (4 * tolerance)))));
}

[[nodiscard]] inline std::uint32_t
cubicSegments(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3,
              double tolerance = defaultFlattenTolerance) noexcept {
    const double deviation = std::max(length(p0 - p1 * 2 + p2), length(p1 - p2 * 2 + p3));
    return std::max<std::uint32_t>(
        1, static_cast<std::uint32_t>(std::ceil(std::sqrt(3 * deviation / (4 * tolerance)))));
}

[[nodiscard]] inline std::uint32_t
arcSegments(double sweep, double radius, double tolerance = defaultFlattenTolerance) noexcept {
    if (!(radius > tolerance)) {
        return 1;
    }
    const double step = 2 * std::acos(1 - tolerance / radius);
    return std::max<std::uint32_t>(1,
                                   static_cast<std::uint32_t>(std::ceil(std::abs(sweep) / step)));
}

[[nodiscard]] inline double parabolaArcLength(double x) noexcept {
    return 0.5 * (x * std::sqrt(1 + x * x) + std::asinh(x));
}

[[nodiscard]] inline Vector2 cubicPoint(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3,
                                        double t) noexcept {
    const double s = 1 - t;
    return p0 * (s * s * s) + p1 * (3 * s * s * t) + p2 * (3 * s * t * t) + p3 * (t * t * t);
}

[[nodiscard]] inline Vector2 quadraticPoint(Vector2 p0, Vector2 p1, Vector2 p2, double t) noexcept {
    const double s = 1 - t;
    return p0 * (s * s) + p1 * (2 * s * t) + p2 * (t * t);
}
}
