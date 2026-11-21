#pragma once

#include "PixelOracle.h"

#include <cmath>

namespace prism::test {
[[nodiscard]] inline Lab labFromLinear(double r, double g, double b) noexcept {
    const double x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047;
    const double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
    const double z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883;
    constexpr double epsilon = 216.0 / 24389.0;
    const auto f = [](double t) {
        return t > epsilon ? std::cbrt(t) : t * 841.0 / 108.0 + 4.0 / 29.0;
    };
    return {116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))};
}
}
