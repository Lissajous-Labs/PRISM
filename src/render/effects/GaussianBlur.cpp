#include "render/effects/GaussianBlur.h"

#include <cmath>

namespace prism::render {
std::vector<float> gaussianWeights(double sigma) {
    const auto radius = static_cast<std::size_t>(std::ceil(3 * std::max(sigma, 1e-6)));
    std::vector<double> raw(radius + 1);
    double total = 0;
    for (std::size_t i = 0; i <= radius; ++i) {
        raw[i] = std::exp(-static_cast<double>(i * i) / (2 * sigma * sigma));
        total += i == 0 ? raw[i] : 2 * raw[i];
    }
    std::vector<float> weights(radius + 1);
    for (std::size_t i = 0; i <= radius; ++i) {
        weights[i] = static_cast<float>(raw[i] / total);
    }
    return weights;
}

std::vector<PairedTap> pairedTaps(std::span<const float> weights) {
    std::vector<PairedTap> taps{{0, weights[0]}};
    for (std::size_t k = 1; k < weights.size(); k += 2) {
        const float first = weights[k];
        const float second = k + 1 < weights.size() ? weights[k + 1] : 0.0f;
        const float combined = first + second;
        taps.push_back({static_cast<float>(k) + second / combined, combined});
    }
    return taps;
}

namespace {
template <class Sample>
void blurPass(const FloatImage &source, FloatImage &output, bool horizontal, Sample &&sample) {
    output.resize(source.width, source.height);
    for (std::uint32_t y = 0; y < source.height; ++y) {
        for (std::uint32_t x = 0; x < source.width; ++x) {
            output.at(x, y) = sample(source, x, y, horizontal);
        }
    }
}
}

void blurSeparable(const FloatImage &source, FloatImage &output, double sigma) {
    const auto weights = gaussianWeights(sigma);
    const auto sample = [&](const FloatImage &image, std::uint32_t x, std::uint32_t y,
                            bool horizontal) {
        Rgba sum{};
        for (std::int64_t i = -static_cast<std::int64_t>(weights.size() - 1);
             i < static_cast<std::int64_t>(weights.size()); ++i) {
            const Rgba texel = horizontal ? image.clamped(x + i, y) : image.clamped(x, y + i);
            const float weight = weights[static_cast<std::size_t>(std::abs(i))];
            for (std::size_t c = 0; c < 4; ++c) {
                sum[c] += weight * texel[c];
            }
        }
        return sum;
    };
    FloatImage intermediate;
    blurPass(source, intermediate, true, sample);
    blurPass(intermediate, output, false, sample);
}

void blurPaired(const FloatImage &source, FloatImage &output, double sigma) {
    const auto taps = pairedTaps(gaussianWeights(sigma));
    const auto sample = [&](const FloatImage &image, std::uint32_t x, std::uint32_t y,
                            bool horizontal) {
        Rgba sum{};
        for (std::size_t t = 0; t < taps.size(); ++t) {
            for (const float side : {1.0f, -1.0f}) {
                if (t == 0 && side < 0) {
                    continue;
                }
                const double offset = side * taps[t].offset;
                const Rgba texel = horizontal ? image.bilinear(x + 0.5 + offset, y + 0.5)
                                              : image.bilinear(x + 0.5, y + 0.5 + offset);
                for (std::size_t c = 0; c < 4; ++c) {
                    sum[c] += taps[t].weight * texel[c];
                }
            }
        }
        return sum;
    };
    FloatImage intermediate;
    blurPass(source, intermediate, true, sample);
    blurPass(intermediate, output, false, sample);
}
}
