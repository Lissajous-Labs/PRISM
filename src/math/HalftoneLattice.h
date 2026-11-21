#pragma once

#include "math/Vector2.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace prism::math {
enum class HalftoneLatticeKind : std::uint8_t { brick, rotated, hexagonal };

struct HalftoneCell {
    std::int32_t x;
    std::int32_t y;
    Vector2 center;
    Vector2 offset;
};

struct HalftoneLattice {
    Vector2 b1;
    Vector2 b2;
    Vector2 origin{};
    HalftoneLatticeKind kind = HalftoneLatticeKind::brick;

    [[nodiscard]] static HalftoneLattice brick(double px, double py, double stagger = 0) noexcept {
        return {{px, 0}, {stagger * px, py}, {}, HalftoneLatticeKind::brick};
    }
    [[nodiscard]] static HalftoneLattice rotated(double pitch, double angle) noexcept {
        const double c = pitch * std::cos(angle), s = pitch * std::sin(angle);
        return {{c, s}, {-s, c}, {}, HalftoneLatticeKind::rotated};
    }
    [[nodiscard]] static HalftoneLattice hexagonal(double pitch) noexcept {
        return {{pitch, 0},
                {pitch * 0.5, pitch * std::sqrt(3.0) * 0.5},
                {},
                HalftoneLatticeKind::hexagonal};
    }
    [[nodiscard]] double area() const noexcept { return std::abs(b1.x * b2.y - b1.y * b2.x); }
    [[nodiscard]] Vector2 coordinates(Vector2 point) const noexcept {
        const double x = point.x - origin.x, y = point.y - origin.y;
        const double det = b1.x * b2.y - b1.y * b2.x;
        return {(b2.y * x - b2.x * y) / det, (b1.x * y - b1.y * x) / det};
    }
    [[nodiscard]] HalftoneCell cell(Vector2 point) const noexcept {
        const auto c = coordinates(point);
        double x, y;
        if (kind == HalftoneLatticeKind::hexagonal) {
            const double q = c.x, r = c.y, s = -q - r;
            x = std::round(q);
            y = std::round(r);
            const double z = std::round(s);
            const double ex = std::abs(x - q), ey = std::abs(y - r), ez = std::abs(z - s);
            if (ex > ey && ex > ez) {
                x = -y - z;
            } else if (ey > ez) {
                y = -x - z;
            }
        } else {
            x = std::floor(c.x);
            y = std::floor(c.y);
        }
        const double shift = kind == HalftoneLatticeKind::hexagonal ? 0 : 0.5;
        const Vector2 center{origin.x + b1.x * (x + shift) + b2.x * (y + shift),
                             origin.y + b1.y * (x + shift) + b2.y * (y + shift)};
        return {static_cast<std::int32_t>(x),
                static_cast<std::int32_t>(y),
                center,
                {point.x - center.x, point.y - center.y}};
    }
};

[[nodiscard]] inline double circleRadiusForArea(double fraction, double cellArea) noexcept {
    return std::sqrt(std::max(0.0, fraction * cellArea) / std::numbers::pi);
}
[[nodiscard]] inline double pillArea(double width, double height, double radius) noexcept {
    const double r = std::clamp(radius, 0.0, 0.5 * std::min(width, height));
    return width * height - (4 - std::numbers::pi) * r * r;
}
[[nodiscard]] inline double pillWidthForArea(double fraction, double cellArea, double height,
                                             double radius) noexcept {
    return (fraction * cellArea + (4 - std::numbers::pi) * radius * radius) / height;
}
[[nodiscard]] inline Vector2 superellipseAxesForArea(double fraction, double cellArea,
                                                     double aspect, double exponent) noexcept {
    const double g = std::tgamma(1 + 1 / exponent);
    const double b = std::sqrt(std::max(0.0, fraction * cellArea) * std::tgamma(1 + 2 / exponent) /
                               (4 * aspect * g * g));
    return {aspect * b, b};
}
[[nodiscard]] inline double halftoneIgnition(double energy, double threshold, double mix = 0.85,
                                             double spread = 0.38, double width = 0.18) noexcept {
    const double e = std::clamp(energy, 0.0, 1.0);
    const double t = std::clamp((e - spread * threshold) / width, 0.0, 1.0);
    return e * (1 - mix + mix * t * t * (3 - 2 * t));
}
}
