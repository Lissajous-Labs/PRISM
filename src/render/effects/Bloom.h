#pragma once

#include "render/effects/FloatImage.h"

namespace prism::render {
struct BloomSettings {
    float threshold = 1.0f;
    float knee = 0.5f;
    float intensity = 0.6f;
    int levels = 4;
    bool karis = true;
};

[[nodiscard]] float bloomLuma(const Rgba &color) noexcept;
[[nodiscard]] float softKneeMultiplier(float luma, float threshold, float knee) noexcept;
void bloomPrefilter(const FloatImage &source, FloatImage &output, const BloomSettings &settings);
void applyBloom(const FloatImage &source, FloatImage &output, const BloomSettings &settings);
}
