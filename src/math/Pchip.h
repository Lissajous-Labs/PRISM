#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace prism::math {
[[nodiscard]] inline double pchipEndSlope(double h0, double h1, double d0, double d1) noexcept {
    const double slope = ((2 * h0 + h1) * d0 - h0 * d1) / (h0 + h1);
    if (slope * d0 <= 0) {
        return 0;
    }
    if (d0 * d1 <= 0 && std::abs(slope) > 3 * std::abs(d0)) {
        return 3 * d0;
    }
    return slope;
}

inline void pchipSlopes(std::span<const double> x, std::span<const double> y,
                        std::span<double> slopes) noexcept {
    const std::size_t n = std::min({x.size(), y.size(), slopes.size()});
    if (n < 2) {
        std::fill(slopes.begin(), slopes.end(), 0.0);
        return;
    }
    if (n == 2) {
        slopes[0] = slopes[1] = (y[1] - y[0]) / (x[1] - x[0]);
        return;
    }

    for (std::size_t i = 1; i + 1 < n; ++i) {
        const double h0 = x[i] - x[i - 1];
        const double h1 = x[i + 1] - x[i];
        const double d0 = (y[i] - y[i - 1]) / h0;
        const double d1 = (y[i + 1] - y[i]) / h1;
        if (d0 * d1 <= 0) {
            slopes[i] = 0;
            continue;
        }
        const double w1 = 2 * h1 + h0;
        const double w2 = h1 + 2 * h0;
        slopes[i] = (w1 + w2) / (w1 / d0 + w2 / d1);
    }

    const double h0 = x[1] - x[0];
    const double h1 = x[2] - x[1];
    slopes[0] = pchipEndSlope(h0, h1, (y[1] - y[0]) / h0, (y[2] - y[1]) / h1);
    const double hn = x[n - 1] - x[n - 2];
    const double hm = x[n - 2] - x[n - 3];
    slopes[n - 1] = pchipEndSlope(hn, hm, (y[n - 1] - y[n - 2]) / hn, (y[n - 2] - y[n - 3]) / hm);
}

[[nodiscard]] inline double pchipEvaluate(double y0, double y1, double m0, double m1, double h,
                                          double t) noexcept {
    const double t2 = t * t;
    const double t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * y0 + (t3 - 2 * t2 + t) * h * m0 + (-2 * t3 + 3 * t2) * y1 +
           (t3 - t2) * h * m1;
}

[[nodiscard]] inline double pchipDerivative(double y0, double y1, double m0, double m1, double h,
                                            double t) noexcept {
    const double t2 = t * t;
    return ((6 * t2 - 6 * t) * y0 + (3 * t2 - 4 * t + 1) * h * m0 + (-6 * t2 + 6 * t) * y1 +
            (3 * t2 - 2 * t) * h * m1) /
           h;
}
}
