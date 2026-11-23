#include "../support/ContourShapes.h"
#include "text/MtsdfGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

namespace {
using namespace prism;
using math::Vector2;

double sampleMedian(const text::GlyphBitmap &bitmap, Vector2 p) {
    const double fx = p.x - bitmap.left - 0.5;
    const double fy = p.y - bitmap.top - 0.5;
    const auto x0 = static_cast<int>(std::floor(fx));
    const auto y0 = static_cast<int>(std::floor(fy));
    const double tx = fx - x0;
    const double ty = fy - y0;
    std::array<double, 3> channels{};
    for (int c = 0; c < 3; ++c) {
        const auto texel = [&](int x, int y) {
            x = std::clamp(x, 0, static_cast<int>(bitmap.width) - 1);
            y = std::clamp(y, 0, static_cast<int>(bitmap.height) - 1);
            return static_cast<double>(
                bitmap.texels[(static_cast<std::size_t>(y) * bitmap.width + x) * 4 + c]);
        };
        const double top = texel(x0, y0) * (1 - tx) + texel(x0 + 1, y0) * tx;
        const double bottom = texel(x0, y0 + 1) * (1 - tx) + texel(x0 + 1, y0 + 1) * tx;
        channels[c] = top * (1 - ty) + bottom * ty;
    }
    return std::max(std::min(channels[0], channels[1]),
                    std::min(std::max(channels[0], channels[1]), channels[2]));
}
}

int main() {
    const text::MtsdfSettings settings;
    double worstRms = 0;
    for (const auto &shape : test::contourShapes()) {
        text::GlyphBitmap bitmap;
        text::generateMtsdf(shape.path, settings, bitmap);
        double sum = 0;
        double peak = 0;
        std::size_t count = 0;
        for (const auto &edge : shape.edges) {
            for (int i = 0; i <= 400; ++i) {
                const Vector2 q = edge(i / 400.0);
                const double error =
                    std::abs((sampleMedian(bitmap, q) - 0.5) * 2 * settings.spread);
                sum += error * error;
                peak = std::max(peak, error);
                ++count;
            }
        }
        const double rms = std::sqrt(sum / static_cast<double>(count));
        worstRms = std::max(worstRms, rms);
        std::cout << shape.name << ": contour RMS " << rms << " px, max " << peak << " px at "
                  << settings.emSize << " ppem\n";
    }
    return worstRms < 0.03 ? 0 : 1;
}
