#pragma once

#include <array>
#include <span>

namespace prism::test {
using Pixel = std::array<double, 4>;

struct PixelDifference {
    double maximumAbsolute;
    double psnr;
};

struct Lab {
    double lightness;
    double a;
    double b;
};

[[nodiscard]] PixelDifference comparePixels(std::span<const Pixel> actual,
                                          std::span<const Pixel> expected,
                                          double peak = 1.0);
[[nodiscard]] double deltaE2000(Lab first, Lab second);
}
