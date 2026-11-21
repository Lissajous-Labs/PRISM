#include "PixelOracle.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace prism::test {
PixelDifference comparePixels(std::span<const Pixel> actual, std::span<const Pixel> expected,
                              double peak) {
    if (actual.empty() || actual.size() != expected.size() || !std::isfinite(peak) || peak <= 0) {
        throw std::invalid_argument("Pixel spans and peak must be valid");
    }
    double maximum = 0;
    double scaledSquares = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        for (std::size_t channel = 0; channel < 4; ++channel) {
            if (!std::isfinite(actual[i][channel]) || !std::isfinite(expected[i][channel])) {
                throw std::invalid_argument("Pixel channels must be finite");
            }
            const double difference = std::abs(actual[i][channel] - expected[i][channel]);
            if (!std::isfinite(difference)) {
                throw std::overflow_error("Pixel difference is not representable");
            }
            if (difference == 0) {
                continue;
            }
            if (difference > maximum) {
                const double ratio = maximum / difference;
                scaledSquares = 1 + scaledSquares * ratio * ratio;
                maximum = difference;
            } else {
                const double ratio = difference / maximum;
                scaledSquares += ratio * ratio;
            }
        }
    }
    if (maximum == 0) {
        return {0, std::numeric_limits<double>::infinity()};
    }
    const double meanScaledSquares = scaledSquares / static_cast<double>(actual.size()) / 4;
    return {maximum,
            20 * (std::log10(peak) - std::log10(maximum)) - 10 * std::log10(meanScaledSquares)};
}

namespace {
constexpr double radians(double degrees) noexcept { return degrees * std::numbers::pi / 180; }

double chromaFraction(double chroma) noexcept {
    if (chroma > 25) {
        return 1 / (1 + std::pow(25 / chroma, 7));
    }
    const double power = std::pow(chroma / 25, 7);
    return power / (1 + power);
}

double hue(double a, double b) noexcept {
    if (a == 0 && b == 0) {
        return 0;
    }
    const double angle = std::atan2(b, a);
    return angle < 0 ? angle + 2 * std::numbers::pi : angle;
}

bool finite(Lab color) noexcept {
    return std::isfinite(color.lightness) && std::isfinite(color.a) && std::isfinite(color.b);
}
}

double deltaE2000(Lab first, Lab second) {
    if (!finite(first) || !finite(second)) {
        throw std::invalid_argument("Lab coordinates must be finite");
    }
    const double chroma1 = std::hypot(first.a, first.b);
    const double chroma2 = std::hypot(second.a, second.b);
    const double meanChroma = chroma1 / 2 + chroma2 / 2;
    const double g = 0.5 * (1 - std::sqrt(chromaFraction(meanChroma)));
    const double a1 = (1 + g) * first.a;
    const double a2 = (1 + g) * second.a;
    const double c1 = std::hypot(a1, first.b);
    const double c2 = std::hypot(a2, second.b);
    const double h1 = hue(a1, first.b);
    const double h2 = hue(a2, second.b);
    const double deltaL = second.lightness - first.lightness;
    const double deltaC = c2 - c1;
    const bool achromatic = c1 == 0 || c2 == 0;
    double deltaHue = achromatic ? 0 : h2 - h1;
    if (deltaHue > std::numbers::pi) {
        deltaHue -= 2 * std::numbers::pi;
    } else if (deltaHue < -std::numbers::pi) {
        deltaHue += 2 * std::numbers::pi;
    }
    const double deltaH = 2 * std::sqrt(c1) * std::sqrt(c2) * std::sin(deltaHue / 2);
    const double meanL = first.lightness / 2 + second.lightness / 2;
    const double meanC = c1 / 2 + c2 / 2;
    double meanHue = h1 + h2;
    if (!achromatic) {
        if (std::abs(h1 - h2) <= std::numbers::pi) {
            meanHue /= 2;
        } else if (meanHue < 2 * std::numbers::pi) {
            meanHue = (meanHue + 2 * std::numbers::pi) / 2;
        } else {
            meanHue = (meanHue - 2 * std::numbers::pi) / 2;
        }
    }
    const double t = 1 - 0.17 * std::cos(meanHue - radians(30)) + 0.24 * std::cos(2 * meanHue) +
                     0.32 * std::cos(3 * meanHue + radians(6)) -
                     0.20 * std::cos(4 * meanHue - radians(63));
    const double centeredL = meanL - 50;
    const double sL = 1 + 0.015 * centeredL * centeredL / std::hypot(centeredL, std::sqrt(20.0));
    const double sC = 1 + 0.045 * meanC;
    const double sH = 1 + 0.015 * meanC * t;
    const double hueOffset = (meanHue - radians(275)) / radians(25);
    const double rotation = -2 * std::sqrt(chromaFraction(meanC)) *
                            std::sin(radians(60) * std::exp(-hueOffset * hueOffset));
    const double l = deltaL / sL;
    const double c = deltaC / sC;
    const double h = deltaH / sH;
    const double squared = l * l + c * c + h * h + rotation * c * h;
    if (!std::isfinite(squared)) {
        throw std::overflow_error("Lab difference is not representable");
    }
    return std::sqrt(std::max(0.0, squared));
}
}
