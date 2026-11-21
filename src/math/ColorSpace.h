#pragma once

#include "Color.h"

#include <array>
#include <cmath>
#include <numbers>

namespace prism::math {
struct LinearRgb {
    double r;
    double g;
    double b;
};

struct EncodedRgb {
    double r;
    double g;
    double b;
};

struct Oklab {
    double lightness;
    double a;
    double b;
};

struct Oklch {
    double lightness;
    double chroma;
    double hue;
};

using Matrix3 = std::array<std::array<double, 3>, 3>;

namespace detail {
[[nodiscard]] constexpr LinearRgb transform(const Matrix3 &matrix, LinearRgb color) noexcept {
    return {matrix[0][0] * color.r + matrix[0][1] * color.g + matrix[0][2] * color.b,
            matrix[1][0] * color.r + matrix[1][1] * color.g + matrix[1][2] * color.b,
            matrix[2][0] * color.r + matrix[2][1] * color.g + matrix[2][2] * color.b};
}

[[nodiscard]] constexpr Matrix3 invert(const Matrix3 &m) noexcept {
    const double c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
    const double c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
    const double c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    const double inverseDeterminant = 1 / (m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02);
    return {
        {{c00 * inverseDeterminant, (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * inverseDeterminant,
          (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * inverseDeterminant},
         {c01 * inverseDeterminant, (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * inverseDeterminant,
          (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * inverseDeterminant},
         {c02 * inverseDeterminant, (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * inverseDeterminant,
          (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * inverseDeterminant}}};
}

[[nodiscard]] constexpr double wrapHue(double hue) noexcept {
    constexpr double turn = 2 * std::numbers::pi;
    while (hue < 0) {
        hue += turn;
    }
    while (hue >= turn) {
        hue -= turn;
    }
    return hue;
}
}

inline constexpr Matrix3 srgbToDisplayP3{{{0.8224619687, 0.1775380313, 0.0000000000},
                                          {0.0331941989, 0.9668058011, 0.0000000000},
                                          {0.0170826307, 0.0723974407, 0.9105199286}}};
inline constexpr Matrix3 displayP3ToSrgb = detail::invert(srgbToDisplayP3);
inline constexpr Matrix3 oklabLms{{{0.4122214708, 0.5363325363, 0.0514459929},
                                   {0.2119034982, 0.6806995451, 0.1073969566},
                                   {0.0883024619, 0.2817188376, 0.6299787005}}};
inline constexpr Matrix3 oklabFromLms{{{0.2104542553, 0.7936177850, -0.0040720468},
                                       {1.9779984951, -2.4285922050, 0.4505937099},
                                       {0.0259040371, 0.7827717662, -0.8086757660}}};
inline constexpr Matrix3 lmsFromOklab = detail::invert(oklabFromLms);
inline constexpr Matrix3 linearFromLms = detail::invert(oklabLms);
inline constexpr double neutralChroma = 1e-6;

[[nodiscard]] inline double srgbDecode(double encoded) noexcept {
    const double magnitude = std::abs(encoded);
    const double linear =
        magnitude <= 0.04045 ? magnitude / 12.92 : std::pow((magnitude + 0.055) / 1.055, 2.4);
    return std::copysign(linear, encoded);
}

[[nodiscard]] inline double srgbEncode(double linear) noexcept {
    const double magnitude = std::abs(linear);
    const double encoded =
        magnitude <= 0.0031308 ? 12.92 * magnitude : 1.055 * std::pow(magnitude, 1 / 2.4) - 0.055;
    return std::copysign(encoded, linear);
}

[[nodiscard]] inline LinearRgb srgbDecode(EncodedRgb color) noexcept {
    return {srgbDecode(color.r), srgbDecode(color.g), srgbDecode(color.b)};
}

[[nodiscard]] inline EncodedRgb srgbEncode(LinearRgb color) noexcept {
    return {srgbEncode(color.r), srgbEncode(color.g), srgbEncode(color.b)};
}

[[nodiscard]] constexpr LinearRgb toDisplayP3(LinearRgb srgb) noexcept {
    return detail::transform(srgbToDisplayP3, srgb);
}

[[nodiscard]] constexpr LinearRgb fromDisplayP3(LinearRgb displayP3) noexcept {
    return detail::transform(displayP3ToSrgb, displayP3);
}

[[nodiscard]] inline Oklab toOklab(LinearRgb color) noexcept {
    const auto lms = detail::transform(oklabLms, color);
    const auto lab =
        detail::transform(oklabFromLms, {std::cbrt(lms.r), std::cbrt(lms.g), std::cbrt(lms.b)});
    return {lab.r, lab.g, lab.b};
}

[[nodiscard]] inline LinearRgb fromOklab(Oklab color) noexcept {
    const auto root = detail::transform(lmsFromOklab, {color.lightness, color.a, color.b});
    return detail::transform(linearFromLms, {root.r * root.r * root.r, root.g * root.g * root.g,
                                             root.b * root.b * root.b});
}

[[nodiscard]] inline Oklch toOklch(Oklab color) noexcept {
    const double chroma = std::hypot(color.a, color.b);
    return {color.lightness, chroma,
            chroma < neutralChroma ? 0 : detail::wrapHue(std::atan2(color.b, color.a))};
}

[[nodiscard]] inline Oklab fromOklch(Oklch color) noexcept {
    return {color.lightness, color.chroma * std::cos(color.hue),
            color.chroma * std::sin(color.hue)};
}

[[nodiscard]] constexpr Oklab mixOklab(Oklab first, Oklab second, double t) noexcept {
    return {first.lightness + (second.lightness - first.lightness) * t,
            first.a + (second.a - first.a) * t, first.b + (second.b - first.b) * t};
}

[[nodiscard]] constexpr double shortestHueDelta(double from, double to) noexcept {
    return detail::wrapHue(to - from + std::numbers::pi) - std::numbers::pi;
}

[[nodiscard]] constexpr Oklch mixOklch(Oklch first, Oklch second, double t) noexcept {
    const bool firstNeutral = first.chroma < neutralChroma;
    const bool secondNeutral = second.chroma < neutralChroma;
    double firstHue = first.hue;
    double secondHue = second.hue;
    if (firstNeutral && !secondNeutral) {
        firstHue = secondHue;
    } else if (secondNeutral && !firstNeutral) {
        secondHue = firstHue;
    }
    return {first.lightness + (second.lightness - first.lightness) * t,
            first.chroma + (second.chroma - first.chroma) * t,
            detail::wrapHue(firstHue + shortestHueDelta(firstHue, secondHue) * t)};
}
}
