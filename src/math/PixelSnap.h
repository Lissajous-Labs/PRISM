#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace prism::math {
struct SnappedStroke {
    std::int64_t width;
    double center;
};

namespace detail {
inline constexpr double snapLimit = 0x1p62;

[[nodiscard]] inline bool validScale(double scale) noexcept {
    return std::isfinite(scale) && scale > 0;
}

[[nodiscard]] inline std::optional<std::int64_t> toPixel(double value) noexcept {
    if (!(std::abs(value) < snapLimit)) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(value);
}
}

[[nodiscard]] inline std::optional<std::int64_t> snapBoundary(double logical,
                                                              double scale) noexcept {
    if (!detail::validScale(scale)) {
        return std::nullopt;
    }
    return detail::toPixel(std::floor(scale * logical + 0.5));
}

[[nodiscard]] inline bool snapPartition(std::span<const double> boundaries, double scale,
                                        std::span<std::int64_t> snapped) noexcept {
    if (boundaries.size() != snapped.size()) {
        return false;
    }
    for (std::size_t i = 0; i < boundaries.size(); ++i) {
        if (i != 0 && !(boundaries[i] >= boundaries[i - 1])) {
            return false;
        }
        const auto pixel = snapBoundary(boundaries[i], scale);
        if (!pixel) {
            return false;
        }
        snapped[i] = *pixel;
    }
    return true;
}

[[nodiscard]] inline std::optional<SnappedStroke> snapStroke(double position, double width,
                                                             double scale) noexcept {
    if (!detail::validScale(scale) || !std::isfinite(width) || !(width > 0)) {
        return std::nullopt;
    }
    const auto snappedWidth = detail::toPixel(std::floor(scale * width + 0.5));
    const auto base = detail::toPixel(std::floor(scale * position));
    const auto rounded = detail::toPixel(std::floor(scale * position + 0.5));
    if (!snappedWidth || !base || !rounded) {
        return std::nullopt;
    }
    const std::int64_t physicalWidth = *snappedWidth < 1 ? 1 : *snappedWidth;
    const double center =
        physicalWidth % 2 != 0 ? static_cast<double>(*base) + 0.5 : static_cast<double>(*rounded);
    return SnappedStroke{physicalWidth, center};
}
}
