#pragma once

#include "math/Vector2.h"
#include "render/PrimitiveDesc.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace prism::render {
[[nodiscard]] inline double radialParameter(math::Vector2 center, math::Vector2 focus,
                                            double radius, math::Vector2 p) noexcept {
    const math::Vector2 axis = center - focus;
    const math::Vector2 offset = p - focus;
    const double a = math::dot(axis, axis) - radius * radius;
    const double b = math::dot(offset, axis);
    const double c = math::dot(offset, offset);
    const double scale = std::max({radius * radius, math::dot(axis, axis), 1e-12});
    if (std::abs(a) <= 1e-9 * scale) {
        return b > 0 ? c / (2 * b) : 0;
    }
    const double root = std::sqrt(std::max(0.0, b * b - a * c));
    const double first = (b + root) / a;
    const double second = (b - root) / a;
    return std::max(first, second) >= 0 ? std::max(first, second) : 0;
}

[[nodiscard]] inline double gradientParameter(GradientShape shape, const float *geometry,
                                              math::Vector2 p) noexcept {
    const math::Vector2 first{geometry[0], geometry[1]};
    const math::Vector2 second{geometry[2], geometry[3]};
    switch (shape) {
    case GradientShape::linear: {
        const math::Vector2 axis = second - first;
        const double lengthSquared = math::dot(axis, axis);
        return lengthSquared > 0 ? std::clamp(math::dot(p - first, axis) / lengthSquared, 0.0, 1.0)
                                 : 0;
    }
    case GradientShape::radial:
        return std::clamp(radialParameter(first, second, geometry[4], p), 0.0, 1.0);
    case GradientShape::conic: {
        const double angle = std::atan2(p.y - first.y, p.x - first.x) - geometry[2];
        const double turns = angle / (2 * std::numbers::pi);
        return turns - std::floor(turns);
    }
    }
    return 0;
}
}
