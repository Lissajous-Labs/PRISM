#pragma once

#include <cmath>

namespace prism::math {
[[nodiscard]] inline double erfApprox(double x) noexcept {
    const double magnitude = std::abs(x);
    const double t = 1 / (1 + 0.3275911 * magnitude);
    const double polynomial =
        ((((1.061405429 * t - 1.453152027) * t + 1.421413741) * t - 0.284496736) * t +
         0.254829592) *
        t;
    return std::copysign(1 - polynomial * std::exp(-magnitude * magnitude), x);
}
}
