#pragma once

#include "Vector2.h"

#include <algorithm>
#include <cmath>

namespace prism::math {
struct CornerRadii {
    double topRight;
    double bottomRight;
    double topLeft;
    double bottomLeft;
};

struct BorderWidths {
    double left;
    double top;
    double right;
    double bottom;
};

struct RoundedRectShape {
    Vector2 center;
    Vector2 half;
    CornerRadii radii;
};

[[nodiscard]] constexpr CornerRadii clampRadii(CornerRadii radii, Vector2 half) noexcept {
    const auto ratio = [](double extent, double sum) { return sum > 0 ? extent / sum : 1.0; };
    const double scale = std::min({1.0, ratio(2 * half.x, radii.topLeft + radii.topRight),
                                   ratio(2 * half.x, radii.bottomLeft + radii.bottomRight),
                                   ratio(2 * half.y, radii.topLeft + radii.bottomLeft),
                                   ratio(2 * half.y, radii.topRight + radii.bottomRight)});
    return {radii.topRight * scale, radii.bottomRight * scale, radii.topLeft * scale,
            radii.bottomLeft * scale};
}

[[nodiscard]] constexpr double quadrantRadius(Vector2 p, CornerRadii radii) noexcept {
    if (p.x > 0) {
        return p.y > 0 ? radii.bottomRight : radii.topRight;
    }
    return p.y > 0 ? radii.bottomLeft : radii.topLeft;
}

[[nodiscard]] inline double roundedRect(Vector2 p, Vector2 half, CornerRadii radii) noexcept {
    const double radius = quadrantRadius(p, radii);
    const Vector2 q = absolute(p) - half + Vector2{radius, radius};
    return std::min(std::max(q.x, q.y), 0.0) + length(maximum(q, 0)) - radius;
}

[[nodiscard]] inline Vector2 roundedRectGradient(Vector2 p, Vector2 half,
                                                 CornerRadii radii) noexcept {
    const double radius = quadrantRadius(p, radii);
    const Vector2 side{p.x > 0 ? 1.0 : -1.0, p.y > 0 ? 1.0 : -1.0};
    const Vector2 q = absolute(p) - half + Vector2{radius, radius};
    if (q.x > 0 && q.y > 0) {
        const double norm = length(q);
        return {side.x * q.x / norm, side.y * q.y / norm};
    }
    return q.x > q.y ? Vector2{side.x, 0} : Vector2{0, side.y};
}

[[nodiscard]] inline double box(Vector2 p, Vector2 half) noexcept {
    return roundedRect(p, half, {0, 0, 0, 0});
}

[[nodiscard]] inline RoundedRectShape innerBorder(Vector2 half, CornerRadii radii,
                                                  BorderWidths widths) noexcept {
    const auto inset = [](double radius, double first, double second) {
        return std::max(0.0, radius - std::max(first, second));
    };
    return {{(widths.left - widths.right) / 2, (widths.top - widths.bottom) / 2},
            {std::max(0.0, half.x - (widths.left + widths.right) / 2),
             std::max(0.0, half.y - (widths.top + widths.bottom) / 2)},
            {inset(radii.topRight, widths.top, widths.right),
             inset(radii.bottomRight, widths.bottom, widths.right),
             inset(radii.topLeft, widths.top, widths.left),
             inset(radii.bottomLeft, widths.bottom, widths.left)}};
}

[[nodiscard]] inline double segment(Vector2 p, Vector2 a, Vector2 b) noexcept {
    const Vector2 edge = b - a;
    const double lengthSquared = dot(edge, edge);
    const double t = lengthSquared > 0 ? std::clamp(dot(p - a, edge) / lengthSquared, 0.0, 1.0) : 0;
    return length(p - (a + edge * t));
}

[[nodiscard]] inline double capsule(Vector2 p, Vector2 a, Vector2 b, double radius) noexcept {
    return segment(p, a, b) - radius;
}

[[nodiscard]] inline double orientedBox(Vector2 p, Vector2 center, Vector2 half,
                                        Vector2 axis) noexcept {
    const Vector2 local = p - center;
    return box({dot(local, axis), cross(axis, local)}, half);
}

[[nodiscard]] inline double triangle(Vector2 p, Vector2 a, Vector2 b, Vector2 c) noexcept {
    const double orientation = cross(b - a, c - a) < 0 ? -1 : 1;
    double distance = std::min({segment(p, a, b), segment(p, b, c), segment(p, c, a)});
    const bool inside = orientation * cross(b - a, p - a) >= 0 &&
                        orientation * cross(c - b, p - b) >= 0 &&
                        orientation * cross(a - c, p - c) >= 0;
    return inside ? -distance : distance;
}

[[nodiscard]] inline double roundedTriangle(Vector2 p, Vector2 a, Vector2 b, Vector2 c,
                                            double radius) noexcept {
    const double sideA = length(c - b);
    const double sideB = length(a - c);
    const double sideC = length(b - a);
    const double perimeter = sideA + sideB + sideC;
    const double area = std::abs(cross(b - a, c - a)) / 2;
    if (!(perimeter > 0) || !(area > 0)) {
        return triangle(p, a, b, c);
    }
    const Vector2 incenter = (a * sideA + b * sideB + c * sideC) * (1 / perimeter);
    const double inradius = 2 * area / perimeter;
    const double rounded = std::clamp(radius, 0.0, inradius);
    const double scale = (inradius - rounded) / inradius;
    const auto shrink = [&](Vector2 vertex) { return incenter + (vertex - incenter) * scale; };
    return triangle(p, shrink(a), shrink(b), shrink(c)) - rounded;
}
}
