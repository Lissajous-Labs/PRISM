#pragma once

#include "SpecialFunctions.h"
#include "Vector2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace prism::math {
namespace detail {
inline constexpr std::array<double, 6> legendreNodes{-0.9324695142, -0.6612093865, -0.2386191861,
                                                     0.2386191861,  0.6612093865,  0.9324695142};
inline constexpr std::array<double, 6> legendreWeights{0.1713244924, 0.3607615730, 0.4679139346,
                                                       0.4679139346, 0.3607615730, 0.1713244924};

[[nodiscard]] inline double erfInterval(double center, double halfWidth, double sigma) noexcept {
    const double scale = 1 / (std::numbers::sqrt2 * sigma);
    return 0.5 *
           (erfApprox((center + halfWidth) * scale) - erfApprox((center - halfWidth) * scale));
}

template <class Integrand>
[[nodiscard]] double gaussLegendre(double low, double high, int panels, Integrand &&integrand) {
    const double panelRadius = (high - low) / (2 * panels);
    double total = 0;
    for (int panel = 0; panel < panels; ++panel) {
        const double middle = low + (2 * panel + 1) * panelRadius;
        for (std::size_t k = 0; k < legendreNodes.size(); ++k) {
            total += legendreWeights[k] * integrand(middle + panelRadius * legendreNodes[k]);
        }
    }
    return total * panelRadius;
}

[[nodiscard]] inline int panelsFor(double extent, double sigma) noexcept {
    return std::clamp(static_cast<int>(std::ceil(extent / (1.5 * sigma))), 1, 8);
}
}

[[nodiscard]] inline double boxShadow(Vector2 p, Vector2 half, double sigma) noexcept {
    if (!(sigma > 0)) {
        return std::abs(p.x) <= half.x && std::abs(p.y) <= half.y ? 1 : 0;
    }
    return detail::erfInterval(p.x, half.x, sigma) * detail::erfInterval(p.y, half.y, sigma);
}

[[nodiscard]] inline double roundedBoxShadow(Vector2 p, Vector2 half, double radius,
                                             double sigma) noexcept {
    radius = std::clamp(radius, 0.0, std::min(half.x, half.y));
    if (radius == 0 || !(sigma > 0)) {
        return boxShadow(p, half, sigma);
    }
    const double reach = 4.5 * sigma;
    const double low = std::max(-half.y, p.y - reach);
    const double high = std::min(half.y, p.y + reach);
    if (!(high > low)) {
        return 0;
    }
    const double kernel = 1 / (std::sqrt(2 * std::numbers::pi) * sigma);
    const auto density = [&](double y) {
        const double offset = p.y - y;
        return kernel * std::exp(-offset * offset / (2 * sigma * sigma));
    };
    const double straight = half.y - radius;
    double total = 0;
    const double middleLow = std::max(low, -straight);
    const double middleHigh = std::min(high, straight);
    if (middleHigh > middleLow) {
        total += detail::erfInterval(p.x, half.x, sigma) *
                 detail::gaussLegendre(middleLow, middleHigh,
                                       detail::panelsFor(middleHigh - middleLow, sigma), density);
    }
    for (const double side : {1.0, -1.0}) {
        const double nearEdge = std::max(straight, side > 0 ? low : -high);
        const double farEdge = std::min(half.y, side > 0 ? high : -low);
        if (!(farEdge > nearEdge)) {
            continue;
        }
        const double start = std::asin(std::clamp((nearEdge - straight) / radius, 0.0, 1.0));
        const double end = std::asin(std::clamp((farEdge - straight) / radius, 0.0, 1.0));
        total += detail::gaussLegendre(
            start, end, detail::panelsFor(radius * (end - start), sigma), [&](double angle) {
                const double y = side * (straight + radius * std::sin(angle));
                const double slice = half.x - radius + radius * std::cos(angle);
                return density(y) * detail::erfInterval(p.x, slice, sigma) * radius *
                       std::cos(angle);
            });
    }
    return total;
}
}
