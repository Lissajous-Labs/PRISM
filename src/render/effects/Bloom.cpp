#include "render/effects/Bloom.h"

#include "render/effects/DualKawase.h"

#include <algorithm>
#include <vector>

namespace prism::render {
float bloomLuma(const Rgba &color) noexcept {
    return 0.2126f * color[0] + 0.7152f * color[1] + 0.0722f * color[2];
}

float softKneeMultiplier(float luma, float threshold, float knee) noexcept {
    const float soft = std::clamp(luma - threshold + knee, 0.0f, 2 * knee);
    const float curve = soft * soft / (4 * knee + 1e-5f);
    return std::max(curve, luma - threshold) / std::max(luma, 1e-5f);
}

void bloomPrefilter(const FloatImage &source, FloatImage &output, const BloomSettings &settings) {
    output.resize(std::max(1u, source.width / 2), std::max(1u, source.height / 2));
    for (std::uint32_t y = 0; y < output.height; ++y) {
        for (std::uint32_t x = 0; x < output.width; ++x) {
            Rgba sum{};
            float weightSum = 0;
            for (std::uint32_t j = 0; j < 2; ++j) {
                for (std::uint32_t i = 0; i < 2; ++i) {
                    const Rgba texel = source.clamped(2 * x + i, 2 * y + j);
                    const float multiplier =
                        softKneeMultiplier(bloomLuma(texel), settings.threshold, settings.knee);
                    const float weight = settings.karis ? 1.0f / (1.0f + bloomLuma(texel)) : 1.0f;
                    for (std::size_t c = 0; c < 4; ++c) {
                        sum[c] += texel[c] * multiplier * weight;
                    }
                    weightSum += weight;
                }
            }
            for (std::size_t c = 0; c < 4; ++c) {
                output.at(x, y)[c] = sum[c] / weightSum;
            }
        }
    }
}

void applyBloom(const FloatImage &source, FloatImage &output, const BloomSettings &settings) {
    std::vector<FloatImage> chain(static_cast<std::size_t>(settings.levels) + 1);
    bloomPrefilter(source, chain[0], settings);
    for (int level = 0; level < settings.levels; ++level) {
        kawaseDown(chain[level], chain[level + 1]);
    }
    FloatImage current = chain[settings.levels];
    for (int level = settings.levels - 1; level >= 0; --level) {
        FloatImage next;
        kawaseUp(current, chain[level].width, chain[level].height, next);
        current = std::move(next);
    }
    FloatImage glow;
    kawaseUp(current, source.width, source.height, glow);
    output.resize(source.width, source.height);
    for (std::size_t i = 0; i < output.pixels.size(); ++i) {
        for (std::size_t c = 0; c < 3; ++c) {
            output.pixels[i][c] = source.pixels[i][c] + settings.intensity * glow.pixels[i][c];
        }
        output.pixels[i][3] = source.pixels[i][3];
    }
}
}
