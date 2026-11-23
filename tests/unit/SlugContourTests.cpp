#include "../support/ContourShapes.h"
#include "text/SlugGlyph.h"

#include <algorithm>
#include <cmath>
#include <iostream>

int main() {
    using namespace prism;
    constexpr double ppem = 144;
    constexpr double unitsPerEm = 48;
    constexpr double pixelsPerUnit = ppem / unitsPerEm;
    constexpr double interactionPx = 1.5;
    constexpr int samples = 400;
    double worstRms = 0;

    for (const auto &shape : test::contourShapes()) {
        const text::SlugGlyph glyph = text::buildSlugGlyph(shape.path, 1e-4);
        const auto alpha = [&](math::Vector2 p) {
            return text::slugCoverage(glyph.blob, p, pixelsPerUnit, pixelsPerUnit);
        };

        std::vector<std::vector<math::Vector2>> dense(shape.edges.size());
        for (std::size_t e = 0; e < shape.edges.size(); ++e) {
            for (int i = 0; i <= samples; ++i) {
                dense[e].push_back(shape.edges[e](static_cast<double>(i) / samples));
            }
        }
        const auto isolated = [&](std::size_t edge, int index) {
            const math::Vector2 q = dense[edge][static_cast<std::size_t>(index)];
            for (std::size_t other = 0; other < dense.size(); ++other) {
                for (int j = 0; j <= samples; ++j) {
                    const bool near = other == edge and std::abs(j - index) < samples / 4;
                    if (!near and math::length(dense[other][static_cast<std::size_t>(j)] - q) *
                                          pixelsPerUnit <
                                      interactionPx) {
                        return false;
                    }
                }
            }
            return true;
        };

        double sum = 0;
        double peak = 0;
        std::size_t count = 0;
        for (std::size_t e = 0; e < shape.edges.size(); ++e) {
            for (int i = 0; i <= samples; ++i) {
                const math::Vector2 q = dense[e][static_cast<std::size_t>(i)];
                if (!isolated(e, i)) {
                    continue;
                }
                const math::Vector2 tangent = shape.tangents[e](static_cast<double>(i) / samples);
                const double tangentLength = math::length(tangent);
                if (tangentLength == 0) {
                    continue;
                }
                const math::Vector2 normal{-tangent.y / tangentLength, tangent.x / tangentLength};
                double low = -1.5 / pixelsPerUnit;
                double high = 1.5 / pixelsPerUnit;
                const bool rising = alpha(q + normal * high) > alpha(q + normal * low);
                for (int k = 0; k < 60; ++k) {
                    const double mid = 0.5 * (low + high);
                    const bool above = alpha(q + normal * mid) > 0.5;
                    (above == rising ? high : low) = mid;
                }
                const double error = std::abs(0.5 * (low + high)) * pixelsPerUnit;
                sum += error * error;
                peak = std::max(peak, error);
                ++count;
            }
        }

        const double rms = std::sqrt(sum / static_cast<double>(count));
        worstRms = std::max(worstRms, rms);
        std::cout << shape.name << ": Slug contour RMS " << rms << " px, max " << peak << " px at "
                  << ppem << " ppem over " << count << " samples\n";
    }
    return worstRms < 0.005 ? 0 : 1;
}
