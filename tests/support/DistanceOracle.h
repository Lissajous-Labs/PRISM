#pragma once

#include "math/Vector2.h"

#include <algorithm>
#include <cmath>

namespace prism::test {
template <class Curve> double curveDistance(math::Vector2 p, Curve curve, int samples = 4096) {
    double bestT = 0;
    double best = math::length(curve(0.0) - p);
    for (int i = 1; i <= samples; ++i) {
        const double t = static_cast<double>(i) / samples;
        const double distance = math::length(curve(t) - p);
        if (distance < best) {
            best = distance;
            bestT = t;
        }
    }
    double low = std::max(0.0, bestT - 1.0 / samples);
    double high = std::min(1.0, bestT + 1.0 / samples);
    constexpr double ratio = 0.6180339887498949;
    for (int i = 0; i < 100; ++i) {
        const double first = high - ratio * (high - low);
        const double second = low + ratio * (high - low);
        if (math::length(curve(first) - p) < math::length(curve(second) - p)) {
            high = second;
        } else {
            low = first;
        }
    }
    return std::min(best, math::length(curve((low + high) / 2) - p));
}
}
