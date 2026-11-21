#pragma once

#include "render/effects/FloatImage.h"

#include <span>
#include <vector>

namespace prism::render {
struct PairedTap {
    float offset;
    float weight;
};

[[nodiscard]] std::vector<float> gaussianWeights(double sigma);
[[nodiscard]] std::vector<PairedTap> pairedTaps(std::span<const float> weights);
void blurSeparable(const FloatImage &source, FloatImage &output, double sigma);
void blurPaired(const FloatImage &source, FloatImage &output, double sigma);
}
