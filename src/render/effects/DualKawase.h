#pragma once

#include "render/effects/FloatImage.h"

namespace prism::render {
struct KawaseLevels {
    int levels;
    float blend;
};

[[nodiscard]] KawaseLevels kawaseLevels(double sigma) noexcept;
void kawaseDown(const FloatImage &source, FloatImage &output);
void kawaseUp(const FloatImage &source, std::uint32_t width, std::uint32_t height,
              FloatImage &output);
void kawaseBlur(const FloatImage &source, FloatImage &output, double sigma);
}
