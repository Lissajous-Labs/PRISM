#pragma once

#include "math/Vector2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace prism::test {
using math::Vector2;

inline double referenceSlice(double y, Vector2 half, double radius) {
    const double corner = std::min(half.y - radius - std::abs(y), 0.0);
    return half.x - radius + std::sqrt(std::max(0.0, radius * radius - corner * corner));
}

inline double referenceIntegrand(Vector2 p, Vector2 half, double radius, double sigma, double y) {
    const double scale = 1 / (std::numbers::sqrt2 * sigma);
    const double slice = referenceSlice(y, half, radius);
    const double inner = 0.5 * (std::erf((p.x + slice) * scale) - std::erf((p.x - slice) * scale));
    const double offset = p.y - y;
    return inner * std::exp(-offset * offset / (2 * sigma * sigma)) /
           (std::sqrt(2 * std::numbers::pi) * sigma);
}

template <class Function>
inline double simpson(Function &&f, double a, double b, double fa, double fm, double fb,
                      double whole, int depth) {
    const double middle = (a + b) / 2;
    const double left = (a + middle) / 2;
    const double right = (middle + b) / 2;
    const double fl = f(left);
    const double fr = f(right);
    const double leftArea = (middle - a) * (fa + 4 * fl + fm) / 6;
    const double rightArea = (b - middle) * (fm + 4 * fr + fb) / 6;
    if (depth <= 0 || std::abs(leftArea + rightArea - whole) < 1e-12) {
        return leftArea + rightArea;
    }
    return simpson(f, a, middle, fa, fl, fm, leftArea, depth - 1) +
           simpson(f, middle, b, fm, fr, fb, rightArea, depth - 1);
}

inline double referenceShadow(Vector2 p, Vector2 half, double radius, double sigma) {
    const auto f = [&](double y) { return referenceIntegrand(p, half, radius, sigma, y); };
    const std::array<double, 4> breaks{-half.y, -(half.y - radius), half.y - radius, half.y};
    double total = 0;
    for (std::size_t i = 0; i + 1 < breaks.size(); ++i) {
        const int pieces = 1024;
        const double width = (breaks[i + 1] - breaks[i]) / pieces;
        for (int piece = 0; piece < pieces; ++piece) {
            const double a = breaks[i] + piece * width;
            const double b = a + width;
            const double fa = f(a);
            const double fm = f((a + b) / 2);
            const double fb = f(b);
            total += simpson(f, a, b, fa, fm, fb, width * (fa + 4 * fm + fb) / 6, 8);
        }
    }
    return total;
}
}
