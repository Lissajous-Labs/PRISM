#pragma once

#include "render/FrameView.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace prism::render {
using Texel = std::array<double, 4>;

[[nodiscard]] inline Texel fetchTexel(const TextureView &texture, std::int64_t x,
                                      std::int64_t y) noexcept {
    x = std::clamp<std::int64_t>(x, 0, texture.width - 1);
    y = std::clamp<std::int64_t>(y, 0, texture.height - 1);
    const std::size_t base =
        (static_cast<std::size_t>(y) * texture.width + static_cast<std::size_t>(x)) *
        texture.channels;
    Texel texel{0, 0, 0, 1};
    for (std::uint32_t c = 0; c < texture.channels && c < 4; ++c) {
        texel[c] = texture.texels[base + c];
    }
    return texel;
}

[[nodiscard]] inline Texel sampleBilinear(const TextureView &texture, double u, double v) noexcept {
    if (texture.width == 0 || texture.height == 0) {
        return {0, 0, 0, 0};
    }
    const double x = u * texture.width - 0.5;
    const double y = v * texture.height - 0.5;
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const double tx = x - fx;
    const double ty = y - fy;
    const auto ix = static_cast<std::int64_t>(fx);
    const auto iy = static_cast<std::int64_t>(fy);
    const Texel a = fetchTexel(texture, ix, iy);
    const Texel b = fetchTexel(texture, ix + 1, iy);
    const Texel c = fetchTexel(texture, ix, iy + 1);
    const Texel d = fetchTexel(texture, ix + 1, iy + 1);
    Texel result{};
    for (std::size_t i = 0; i < 4; ++i) {
        result[i] = (a[i] * (1 - tx) + b[i] * tx) * (1 - ty) + (c[i] * (1 - tx) + d[i] * tx) * ty;
    }
    return result;
}

inline constexpr double filterSubtexelSteps = 256;

[[nodiscard]] inline Texel sampleFiltered(const TextureView &texture, double u, double v) noexcept {
    if (texture.width == 0 || texture.height == 0) {
        return {0, 0, 0, 0};
    }
    const double x = u * texture.width - 0.5;
    const double y = v * texture.height - 0.5;
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const double tx = std::round((x - fx) * filterSubtexelSteps) / filterSubtexelSteps;
    const double ty = std::round((y - fy) * filterSubtexelSteps) / filterSubtexelSteps;
    const auto ix = static_cast<std::int64_t>(fx);
    const auto iy = static_cast<std::int64_t>(fy);
    const Texel a = fetchTexel(texture, ix, iy);
    const Texel b = fetchTexel(texture, ix + 1, iy);
    const Texel c = fetchTexel(texture, ix, iy + 1);
    const Texel d = fetchTexel(texture, ix + 1, iy + 1);
    Texel result{};
    for (std::size_t i = 0; i < 4; ++i) {
        result[i] = (a[i] * (1 - tx) + b[i] * tx) * (1 - ty) + (c[i] * (1 - tx) + d[i] * tx) * ty;
    }
    return result;
}
}
