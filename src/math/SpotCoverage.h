#pragma once

#include "math/Vector2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace prism::math {
struct CoveragePolygon {
    std::array<Vector2, 16> points{};
    std::size_t count = 0;
};
[[nodiscard]] inline double coverageCross(Vector2 a, Vector2 b) noexcept {
    return a.x * b.y - a.y * b.x;
}
[[nodiscard]] inline double coveragePolygonArea(const CoveragePolygon &p) noexcept {
    double result = 0;
    for (std::size_t i = 0; i < p.count; ++i) {
        result += coverageCross(p.points[i], p.points[(i + 1) % p.count]);
    }
    return std::abs(result) * 0.5;
}
[[nodiscard]] inline CoveragePolygon coverageClip(const CoveragePolygon &p, int axis,
                                                  double boundary, bool greater) noexcept {
    CoveragePolygon result;
    const auto coordinate = [axis](Vector2 v) { return axis == 0 ? v.x : v.y; };
    for (std::size_t i = 0; i < p.count; ++i) {
        const auto a = p.points[i], b = p.points[(i + 1) % p.count];
        const double ca = coordinate(a) - boundary, cb = coordinate(b) - boundary;
        const bool insideA = greater ? ca >= 0 : ca <= 0;
        const bool insideB = greater ? cb >= 0 : cb <= 0;
        if (insideA) {
            result.points[result.count++] = a;
        }
        if (insideA != insideB) {
            const double t = ca / (ca - cb);
            result.points[result.count++] = {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
        }
    }
    return result;
}
[[nodiscard]] inline CoveragePolygon coverageBox(const CoveragePolygon &p, Vector2 low,
                                                 Vector2 high) noexcept {
    return coverageClip(
        coverageClip(coverageClip(coverageClip(p, 0, low.x, true), 0, high.x, false), 1, low.y,
                     true),
        1, high.y, false);
}
[[nodiscard]] inline double coverageDisk(const CoveragePolygon &p, Vector2 center,
                                         double radius) noexcept {
    double area = 0;
    const double rr = radius * radius;
    for (std::size_t i = 0; i < p.count; ++i) {
        const Vector2 a = p.points[i] - center, b = p.points[(i + 1) % p.count] - center;
        const Vector2 delta = b - a;
        const double aa = dot(delta, delta), bb = 2 * dot(a, delta), cc = dot(a, a) - rr;
        std::array<double, 4> cuts{0, 0, 0, 0};
        std::size_t count = 1;
        const double discriminant = bb * bb - 4 * aa * cc;
        if (aa > 0 && discriminant > 0) {
            const double root = std::sqrt(discriminant);
            for (double t : {(-bb - root) / (2 * aa), (-bb + root) / (2 * aa)}) {
                if (t > 0 && t < 1) {
                    cuts[count++] = t;
                }
            }
        }
        cuts[count++] = 1;
        for (std::size_t j = 1; j < count; ++j) {
            const Vector2 u = a + delta * cuts[j - 1], v = a + delta * cuts[j];
            const Vector2 midpoint = (u + v) * 0.5;
            area += dot(midpoint, midpoint) <= rr
                        ? coverageCross(u, v) * 0.5
                        : rr * std::atan2(coverageCross(u, v), dot(u, v)) * 0.5;
        }
    }
    return std::abs(area);
}
[[nodiscard]] inline double roundedSpotPixelArea(const CoveragePolygon &pixel, Vector2 half,
                                                 double radius) noexcept {
    const double r = std::clamp(radius, 0.0, std::min(half.x, half.y));
    if (r == 0) {
        return coveragePolygonArea(coverageBox(pixel, {-half.x, -half.y}, half));
    }
    double area =
        coveragePolygonArea(coverageBox(pixel, {-half.x + r, -half.y}, {half.x - r, half.y}));
    for (double side : {-1.0, 1.0}) {
        const double x0 = side < 0 ? -half.x : half.x - r;
        const double x1 = side < 0 ? -half.x + r : half.x;
        area += coveragePolygonArea(coverageBox(pixel, {x0, -half.y + r}, {x1, half.y - r}));
        for (double end : {-1.0, 1.0}) {
            const double y0 = end < 0 ? -half.y : half.y - r;
            const double y1 = end < 0 ? -half.y + r : half.y;
            area += coverageDisk(coverageBox(pixel, {x0, y0}, {x1, y1}),
                                 {side * (half.x - r), end * (half.y - r)}, r);
        }
    }
    return area;
}
}
