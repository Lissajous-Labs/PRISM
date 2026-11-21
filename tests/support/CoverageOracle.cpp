#include "CoverageOracle.h"

#include <cmath>
#include <stdexcept>

namespace prism::test {
namespace {
std::uint64_t scramble(std::uint64_t value) noexcept {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

double jitter(std::uint64_t seed, std::uint64_t index) noexcept {
    const auto bits = scramble(seed + 0x9e3779b97f4a7c15ULL * (index + 1)) >> 32;
    return (static_cast<double>(bits) + 0.5) * 0x1p-32;
}

bool inside(std::span<const Contour> contours, Point sample, FillRule rule) noexcept {
    std::int64_t winding = 0;
    bool parity = false;
    for (const auto contour : contours) {
        if (contour.size() < 3) {
            continue;
        }
        Point previous = contour.back();
        for (const Point current : contour) {
            const bool up = previous.y <= sample.y && current.y > sample.y;
            const bool down = current.y <= sample.y && previous.y > sample.y;
            if (up || down) {
                const long double dx = static_cast<long double>(current.x) - previous.x;
                const long double dy = static_cast<long double>(current.y) - previous.y;
                const long double px = static_cast<long double>(sample.x) - previous.x;
                const long double py = static_cast<long double>(sample.y) - previous.y;
                const long double side = dx * py - dy * px;
                if ((up && side > 0) || (down && side < 0)) {
                    winding += up ? 1 : -1;
                    parity = !parity;
                }
            }
            previous = current;
        }
    }
    return rule == FillRule::nonZero ? winding != 0 : parity;
}
}

double stratifiedCoverage(std::span<const Contour> contours, Point pixelOrigin, FillRule rule,
                          std::uint64_t seed) {
    if (!std::isfinite(pixelOrigin.x) || !std::isfinite(pixelOrigin.y)
        || pixelOrigin.x + 1 == pixelOrigin.x || pixelOrigin.y + 1 == pixelOrigin.y) {
        throw std::invalid_argument("Pixel origin must support unit-pixel coordinates");
    }
    for (const auto contour : contours) {
        for (const auto point : contour) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
                throw std::invalid_argument("Contour coordinates must be finite");
            }
        }
    }
    constexpr std::uint64_t strata = 64;
    std::uint64_t hits = 0;
    for (std::uint64_t y = 0; y < strata; ++y) {
        for (std::uint64_t x = 0; x < strata; ++x) {
            const auto index = y * strata + x;
            const Point sample{pixelOrigin.x + (static_cast<double>(x) + jitter(seed, 2 * index)) / strata,
                               pixelOrigin.y + (static_cast<double>(y) + jitter(seed, 2 * index + 1)) / strata};
            hits += inside(contours, sample, rule) ? 1 : 0;
        }
    }
    return static_cast<double>(hits) / static_cast<double>(strata * strata);
}
}
