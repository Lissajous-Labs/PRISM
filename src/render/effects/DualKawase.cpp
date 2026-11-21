#include "render/effects/DualKawase.h"

#include <cmath>
#include <vector>

namespace prism::render {
KawaseLevels kawaseLevels(double sigma) noexcept {
    const double exact = std::log2(std::max(sigma, 1.5) / 1.5);
    const int levels = std::max(1, static_cast<int>(std::floor(exact)));
    return {levels, static_cast<float>(std::clamp(exact - levels, 0.0, 1.0))};
}

void kawaseDown(const FloatImage &source, FloatImage &output) {
    output.resize(std::max(1u, source.width / 2), std::max(1u, source.height / 2));
    for (std::uint32_t y = 0; y < output.height; ++y) {
        for (std::uint32_t x = 0; x < output.width; ++x) {
            const double cx = 2.0 * x + 1.0;
            const double cy = 2.0 * y + 1.0;
            const Rgba center = source.bilinear(cx, cy);
            const Rgba corners[4] = {
                source.bilinear(cx - 1, cy - 1), source.bilinear(cx + 1, cy - 1),
                source.bilinear(cx - 1, cy + 1), source.bilinear(cx + 1, cy + 1)};
            Rgba &out = output.at(x, y);
            for (std::size_t c = 0; c < 4; ++c) {
                out[c] = 0.5f * center[c] +
                         0.125f * (corners[0][c] + corners[1][c] + corners[2][c] + corners[3][c]);
            }
        }
    }
}

void kawaseUp(const FloatImage &source, std::uint32_t width, std::uint32_t height,
              FloatImage &output) {
    output.resize(width, height);
    const double scaleX = static_cast<double>(source.width) / width;
    const double scaleY = static_cast<double>(source.height) / height;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const double cx = (x + 0.5) * scaleX;
            const double cy = (y + 0.5) * scaleY;
            const Rgba cardinal[4] = {source.bilinear(cx - 1, cy), source.bilinear(cx + 1, cy),
                                      source.bilinear(cx, cy - 1), source.bilinear(cx, cy + 1)};
            const Rgba diagonal[4] = {
                source.bilinear(cx - 0.5, cy - 0.5), source.bilinear(cx + 0.5, cy - 0.5),
                source.bilinear(cx - 0.5, cy + 0.5), source.bilinear(cx + 0.5, cy + 0.5)};
            Rgba &out = output.at(x, y);
            for (std::size_t c = 0; c < 4; ++c) {
                out[c] =
                    (cardinal[0][c] + cardinal[1][c] + cardinal[2][c] + cardinal[3][c]) / 12.0f +
                    (diagonal[0][c] + diagonal[1][c] + diagonal[2][c] + diagonal[3][c]) / 6.0f;
            }
        }
    }
}

namespace {
void blurLevels(const FloatImage &source, FloatImage &output, int levels) {
    std::vector<FloatImage> chain(static_cast<std::size_t>(levels) + 1);
    chain[0] = source;
    for (int level = 0; level < levels; ++level) {
        kawaseDown(chain[level], chain[level + 1]);
    }
    FloatImage current = chain[levels];
    for (int level = levels - 1; level >= 0; --level) {
        FloatImage next;
        kawaseUp(current, chain[level].width, chain[level].height, next);
        current = std::move(next);
    }
    output = std::move(current);
}
}

void kawaseBlur(const FloatImage &source, FloatImage &output, double sigma) {
    const auto levels = kawaseLevels(sigma);
    FloatImage lower;
    blurLevels(source, lower, levels.levels);
    if (levels.blend <= 0) {
        output = std::move(lower);
        return;
    }
    FloatImage upper;
    blurLevels(source, upper, levels.levels + 1);
    output.resize(source.width, source.height);
    for (std::size_t i = 0; i < output.pixels.size(); ++i) {
        for (std::size_t c = 0; c < 4; ++c) {
            output.pixels[i][c] =
                lower.pixels[i][c] + (upper.pixels[i][c] - lower.pixels[i][c]) * levels.blend;
        }
    }
}
}
