#pragma once

#include "Vector2.h"

#include <algorithm>
#include <cmath>

namespace prism::math {
namespace detail {
struct SuperellipsePiece {
    Vector2 half;
    double power;
    double cut;
    bool swapped;

    [[nodiscard]] Vector2 point(double t) const noexcept {
        const double u = t * cut;
        const double v = std::pow(1 - std::pow(u, power), 1 / power);
        return swapped ? Vector2{half.x * v, half.y * u} : Vector2{half.x * u, half.y * v};
    }

    [[nodiscard]] Vector2 tangent(double t) const noexcept {
        const double u = t * cut;
        const double dv =
            -cut * std::pow(u, power - 1) * std::pow(1 - std::pow(u, power), 1 / power - 1);
        return swapped ? Vector2{half.x * dv, half.y * cut} : Vector2{half.x * cut, half.y * dv};
    }
};

[[nodiscard]] inline double closestOnPiece(Vector2 q, const SuperellipsePiece &piece) noexcept {
    constexpr int samples = 32;
    double t = 0;
    double best = length(piece.point(0) - q);
    for (int i = 1; i <= samples; ++i) {
        const double candidate = static_cast<double>(i) / samples;
        const double distance = length(piece.point(candidate) - q);
        if (distance < best) {
            best = distance;
            t = candidate;
        }
    }
    const auto slope = [&](double s) { return dot(piece.point(s) - q, piece.tangent(s)); };
    for (int i = 0; i < 12; ++i) {
        const double low = std::max(0.0, t - 1e-7);
        const double high = std::min(1.0, t + 1e-7);
        const double curvature = (slope(high) - slope(low)) / (high - low);
        if (!(curvature > 0)) {
            break;
        }
        t = std::clamp(t - slope(t) / curvature, 0.0, 1.0);
        best = std::min(best, length(piece.point(t) - q));
    }
    return best;
}
}

[[nodiscard]] inline double superellipse(Vector2 p, Vector2 half, double power) noexcept {
    const Vector2 q = absolute(p);
    const double radial =
        std::pow(std::pow(q.x / half.x, power) + std::pow(q.y / half.y, power), 1 / power);
    if (!(radial > 0)) {
        return -std::min(half.x, half.y);
    }
    const double cut = std::pow(0.5, 1 / power);
    const double distance = std::min(detail::closestOnPiece(q, {half, power, cut, false}),
                                     detail::closestOnPiece(q, {half, power, cut, true}));
    return radial < 1 ? -distance : distance;
}
}
