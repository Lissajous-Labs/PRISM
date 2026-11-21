#pragma once

#include "math/Dither.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace prism::render {
[[nodiscard]] inline double presentationNoise(std::uint32_t x, std::uint32_t y,
                                              std::uint32_t frame) noexcept {
    const double offset = 5.588238 * static_cast<double>(frame % 64);
    return math::interleavedGradientNoise(static_cast<double>(x) + offset,
                                          static_cast<double>(y) + offset);
}

inline void presentDithered(std::span<const float> linear, std::uint32_t width,
                            std::uint32_t height, std::uint32_t frame,
                            std::span<std::uint8_t> encoded) noexcept {
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t base = (static_cast<std::size_t>(y) * width + x) * 4;
            const double noise = presentationNoise(x, y, frame);
            for (std::size_t c = 0; c < 3; ++c) {
                encoded[base + c] = math::quantizeLinear(linear[base + c], noise);
            }
            encoded[base + 3] = static_cast<std::uint8_t>(
                std::clamp(std::floor(linear[base + 3] * 255.0 + 0.5), 0.0, 255.0));
        }
    }
}
}
