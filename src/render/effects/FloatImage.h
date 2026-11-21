#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace prism::render {
using Rgba = std::array<float, 4>;

struct FloatImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<Rgba> pixels;

    void resize(std::uint32_t w, std::uint32_t h) {
        width = w;
        height = h;
        pixels.assign(static_cast<std::size_t>(w) * h, Rgba{0, 0, 0, 0});
    }

    [[nodiscard]] Rgba &at(std::uint32_t x, std::uint32_t y) noexcept {
        return pixels[static_cast<std::size_t>(y) * width + x];
    }

    [[nodiscard]] Rgba clamped(std::int64_t x, std::int64_t y) const noexcept {
        x = std::clamp<std::int64_t>(x, 0, static_cast<std::int64_t>(width) - 1);
        y = std::clamp<std::int64_t>(y, 0, static_cast<std::int64_t>(height) - 1);
        return pixels[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
    }

    [[nodiscard]] Rgba bilinear(double x, double y) const noexcept {
        const double fx = std::floor(x - 0.5);
        const double fy = std::floor(y - 0.5);
        const double tx = x - 0.5 - fx;
        const double ty = y - 0.5 - fy;
        const auto ix = static_cast<std::int64_t>(fx);
        const auto iy = static_cast<std::int64_t>(fy);
        const Rgba a = clamped(ix, iy);
        const Rgba b = clamped(ix + 1, iy);
        const Rgba c = clamped(ix, iy + 1);
        const Rgba d = clamped(ix + 1, iy + 1);
        Rgba result{};
        for (std::size_t i = 0; i < 4; ++i) {
            result[i] = static_cast<float>((a[i] * (1 - tx) + b[i] * tx) * (1 - ty) +
                                           (c[i] * (1 - tx) + d[i] * tx) * ty);
        }
        return result;
    }
};
}
