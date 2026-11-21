#pragma once

#include <cstdint>
#include <span>

namespace prism::test {
struct Point {
    double x;
    double y;
};

using Contour = std::span<const Point>;
enum class FillRule { nonZero, evenOdd };

[[nodiscard]] double stratifiedCoverage(std::span<const Contour> contours, Point pixelOrigin,
                                        FillRule rule = FillRule::nonZero,
                                        std::uint64_t seed = 0);
}
