#pragma once

#include "SpecialFunctions.h"
#include "Vector2.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace prism::math {
struct BorderWeights {
    double fill;
    double border;
};

[[nodiscard]] inline double boxCoverage(double distance, Vector2 normal) noexcept {
    const double ax = std::abs(normal.x);
    const double ay = std::abs(normal.y);
    const double norm = std::hypot(ax, ay);
    const double u = norm > 0 ? std::max(ax, ay) / norm : 1;
    const double v = norm > 0 ? std::min(ax, ay) / norm : 0;
    const double t = std::abs(distance);
    const double lower = (u - v) / 2;
    const double upper = (u + v) / 2;
    double outside = 0;
    if (t <= lower) {
        outside = 0.5 - t / u;
    } else if (t < upper) {
        outside = (upper - t) * (upper - t) / (2 * u * v);
    }
    outside = std::clamp(outside, 0.0, 1.0);
    return distance >= 0 ? outside : 1 - outside;
}

[[nodiscard]] inline double gaussianCoverage(double distance, double sigma = 0.45) noexcept {
    return 0.5 - 0.5 * erfApprox(distance / (std::numbers::sqrt2 * sigma));
}

[[nodiscard]] constexpr BorderWeights borderWeights(double outer, double inner) noexcept {
    return {outer * inner, outer * (1 - inner)};
}
}
