#include "text/GlyphRasterizer.h"

#include "render/vector/FineRaster.h"
#include "render/vector/VectorPath.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace prism::text {
namespace {
struct Coverage {
    std::uint32_t width;
    std::uint32_t height;
    float left;
    float top;
    std::vector<float> values;
};

Coverage coverageOf(const FontFile &font, std::uint16_t glyph, double scaleX, double scaleY,
                    double offsetX) {
    render::VectorPath path;
    font.outline(glyph, 1.0, path);
    std::vector<render::Polyline> polylines;
    render::flattenPolylines(path, math::Affine2D::scale(scaleX, scaleY), 0.05, polylines);
    double x0 = std::numeric_limits<double>::infinity();
    double y0 = x0;
    double x1 = -x0;
    double y1 = -x0;
    for (const auto &polyline : polylines) {
        for (const auto &p : polyline.points) {
            x0 = std::min(x0, p.x + offsetX);
            y0 = std::min(y0, p.y);
            x1 = std::max(x1, p.x + offsetX);
            y1 = std::max(y1, p.y);
        }
    }
    if (polylines.empty()) {
        return {0, 0, 0, 0, {}};
    }
    const double left = std::floor(x0) - 1;
    const double top = std::floor(y0) - 1;
    const auto width = static_cast<std::uint32_t>(std::ceil(x1) - left + 1);
    const auto height = static_cast<std::uint32_t>(std::ceil(y1) - top + 1);
    for (auto &polyline : polylines) {
        for (auto &p : polyline.points) {
            p = {p.x + offsetX - left, p.y - top};
        }
    }
    std::vector<render::LineSegment> segments;
    render::appendFill(polylines, segments);
    render::PathTiler tiler;
    tiler.run(segments, width, height);
    Coverage result{width, height, static_cast<float>(left), static_cast<float>(top),
                    std::vector<float>(static_cast<std::size_t>(width) * height)};
    render::rasterizeCoverage(tiler, render::FillRule::nonZero, width, height, result.values);
    return result;
}
}

void rasterizeAlpha(const FontFile &font, std::uint16_t glyph, float pixelSize, std::uint32_t phase,
                    GlyphBitmap &bitmap) {
    const double scale = pixelSize / font.metrics().unitsPerEm;
    Coverage coverage =
        coverageOf(font, glyph, scale, scale, static_cast<double>(phase) / subpixelPhases);
    bitmap = {coverage.width,
              coverage.height,
              1,
              coverage.left,
              coverage.top,
              1,
              std::move(coverage.values)};
}

void rasterizeLcd(const FontFile &font, std::uint16_t glyph, float pixelSize, std::uint32_t phase,
                  GlyphBitmap &bitmap) {
    const double scale = pixelSize / font.metrics().unitsPerEm;
    const Coverage sub = coverageOf(font, glyph, 3 * scale, scale, 3.0 * phase / subpixelPhases);
    if (sub.width == 0) {
        bitmap = {};
        return;
    }
    const std::uint32_t leftPad =
        static_cast<std::uint32_t>(std::fmod(std::fmod(sub.left, 3.0f) + 3.0f, 3.0f));
    const std::uint32_t width = (sub.width + leftPad + 4) / 3 + 1;
    bitmap = {width,
              sub.height,
              4,
              std::floor((sub.left - static_cast<float>(leftPad)) / 3.0f),
              sub.top,
              1,
              std::vector<float>(static_cast<std::size_t>(width) * sub.height * 4, 0.0f)};
    constexpr float filter[5] = {1.0f / 9, 2.0f / 9, 3.0f / 9, 2.0f / 9, 1.0f / 9};
    const auto sample = [&](std::int64_t x, std::uint32_t y) {
        const std::int64_t local = x - static_cast<std::int64_t>(leftPad);
        return local >= 0 && local < sub.width
                   ? sub.values[static_cast<std::size_t>(y) * sub.width +
                                static_cast<std::size_t>(local)]
                   : 0.0f;
    };
    for (std::uint32_t y = 0; y < sub.height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            float channels[3] = {0, 0, 0};
            for (std::uint32_t c = 0; c < 3; ++c) {
                const std::int64_t center = static_cast<std::int64_t>(3 * x + c);
                for (int tap = -2; tap <= 2; ++tap) {
                    channels[c] += filter[tap + 2] * sample(center + tap, y);
                }
            }
            float *out = &bitmap.texels[(static_cast<std::size_t>(y) * width + x) * 4];
            out[0] = channels[0];
            out[1] = channels[1];
            out[2] = channels[2];
            out[3] = (channels[0] + channels[1] + channels[2]) / 3;
        }
    }
}
}
