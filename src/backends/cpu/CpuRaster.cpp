#include "backends/cpu/CpuRaster.h"

#include "render/FormantCurve.h"
#include "render/PrimitiveShading.h"
#include "render/ShadingSupport.h"
#include "render/Wavetable3d.h"
#include "render/vector/FineRaster.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace prism::backends {
void CpuRaster::resize(std::uint32_t width, std::uint32_t height) {
    width_ = width;
    height_ = height;
    tilesX_ = (width + rasterTileSize - 1) / rasterTileSize;
    tilesY_ = (height + rasterTileSize - 1) / rasterTileSize;
    pixels_.assign(static_cast<std::size_t>(width) * height, {0, 0, 0, 0});
    tileCounts_.assign(static_cast<std::size_t>(tilesX_) * tilesY_, 0);
    tileOffsets_.assign(tileCounts_.size() + 1, 0);
}

void CpuRaster::bin(const render::FrameView &frame) {
    bounds_.resize(frame.primitives.size());
    std::fill(tileCounts_.begin(), tileCounts_.end(), 0);
    for (std::size_t i = 0; i < frame.primitives.size(); ++i) {
        const render::PrimitiveDesc &desc = frame.primitives[i];
        const auto transform = frame.transforms[desc.transformId].forwardTransform();
        double x0 = std::numeric_limits<double>::infinity();
        double y0 = x0;
        double x1 = -x0;
        double y1 = -x0;
        for (const auto corner : {math::Point2{desc.bounds[0], desc.bounds[1]},
                                  math::Point2{desc.bounds[2], desc.bounds[1]},
                                  math::Point2{desc.bounds[0], desc.bounds[3]},
                                  math::Point2{desc.bounds[2], desc.bounds[3]}}) {
            const auto mapped = transform.apply(corner);
            x0 = std::min(x0, mapped.x);
            y0 = std::min(y0, mapped.y);
            x1 = std::max(x1, mapped.x);
            y1 = std::max(y1, mapped.y);
        }
        const auto clampTo = [](double value, std::uint32_t limit) {
            return static_cast<std::int32_t>(std::clamp(value, 0.0, static_cast<double>(limit)));
        };
        Bounds &b = bounds_[i];
        b = {clampTo(std::floor(x0), width_), clampTo(std::floor(y0), height_),
             clampTo(std::ceil(x1) + 1, width_), clampTo(std::ceil(y1) + 1, height_), 0};
        if (b.x1 <= b.x0 || b.y1 <= b.y0) {
            continue;
        }
        for (std::int32_t ty = b.y0 / static_cast<std::int32_t>(rasterTileSize);
             ty <= (b.y1 - 1) / static_cast<std::int32_t>(rasterTileSize); ++ty) {
            for (std::int32_t tx = b.x0 / static_cast<std::int32_t>(rasterTileSize);
                 tx <= (b.x1 - 1) / static_cast<std::int32_t>(rasterTileSize); ++tx) {
                ++tileCounts_[static_cast<std::size_t>(ty) * tilesX_ +
                              static_cast<std::size_t>(tx)];
            }
        }
    }
    tileOffsets_[0] = 0;
    for (std::size_t t = 0; t < tileCounts_.size(); ++t) {
        tileOffsets_[t + 1] = tileOffsets_[t] + tileCounts_[t];
    }
    tileEntries_.resize(tileOffsets_.back());
    std::fill(tileCounts_.begin(), tileCounts_.end(), 0);
    for (std::size_t i = 0; i < frame.primitives.size(); ++i) {
        const Bounds &b = bounds_[i];
        if (b.x1 <= b.x0 || b.y1 <= b.y0) {
            continue;
        }
        for (std::int32_t ty = b.y0 / static_cast<std::int32_t>(rasterTileSize);
             ty <= (b.y1 - 1) / static_cast<std::int32_t>(rasterTileSize); ++ty) {
            for (std::int32_t tx = b.x0 / static_cast<std::int32_t>(rasterTileSize);
                 tx <= (b.x1 - 1) / static_cast<std::int32_t>(rasterTileSize); ++tx) {
                const std::size_t tile =
                    static_cast<std::size_t>(ty) * tilesX_ + static_cast<std::size_t>(tx);
                tileEntries_[tileOffsets_[tile] + tileCounts_[tile]++] =
                    static_cast<std::uint32_t>(i);
            }
        }
    }
}

void CpuRaster::rasterizePaths(const render::FrameView &frame) {
    if (pathCoverage_.size() < frame.paths.size()) {
        pathCoverage_.resize(frame.paths.size());
    }
    for (std::size_t i = 0; i < frame.paths.size(); ++i) {
        const render::PathDraw &draw = frame.paths[i];
        pathCoverage_[i].resize(static_cast<std::size_t>(width_) * height_);
        tiler_.run(frame.pathSegments.subspan(draw.firstSegment, draw.segmentCount), width_,
                   height_);
        render::rasterizeCoverage(tiler_, draw.rule, width_, height_, pathCoverage_[i]);
    }
}

math::PremultipliedColor CpuRaster::applyPath(const render::FrameView &frame, std::size_t path,
                                              std::int32_t x, std::int32_t y,
                                              math::PremultipliedColor color) const {
    const render::PathDraw &draw = frame.paths[path];
    double coverage =
        pathCoverage_[path][static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x)];
    if (coverage <= 0) {
        return color;
    }
    if (draw.clipId != render::noClip) {
        coverage *= render::clipCoverage(frame, draw.clipId, x + 0.5, y + 0.5);
    }
    const auto source = render::detail::scale(render::detail::decodeColor(draw.color), coverage);
    return math::sourceOver(source, color);
}

void CpuRaster::shadeRange(const render::FrameView &frame, hal::Rect rect, const Segment &segment) {
    for (std::int32_t y = rect.y0; y < rect.y1; ++y) {
        for (std::int32_t x = rect.x0; x < rect.x1; ++x) {
            const std::size_t tile =
                static_cast<std::size_t>(y / static_cast<std::int32_t>(rasterTileSize)) * tilesX_ +
                static_cast<std::size_t>(x / static_cast<std::int32_t>(rasterTileSize));
            math::PremultipliedColor &target =
                pixels_[static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x)];
            math::PremultipliedColor color = segment.clear ? *segment.clear : target;
            std::size_t path = segment.pathBegin;
            for (std::uint32_t e = tileOffsets_[tile]; e < tileOffsets_[tile + 1]; ++e) {
                const std::uint32_t index = tileEntries_[e];
                if (index < segment.primitiveBegin || index >= segment.primitiveEnd) {
                    continue;
                }
                while (path < segment.pathEnd && frame.paths[path].insertAfter <= index) {
                    color = applyPath(frame, path++, x, y, color);
                }
                const Bounds &b = bounds_[index];
                if (x < b.x0 || x >= b.x1 || y < b.y0 || y >= b.y1) {
                    continue;
                }
                const render::PrimitiveDesc &desc = frame.primitives[index];
                render::ShadeResult shaded;
                if (desc.kind() == render::PrimitiveKind::wavetable3d ||
                    desc.kind() == render::PrimitiveKind::formantCurve) {
                    const auto texel = analyticImages_[b.analytic].at(
                        static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                    shaded = {{texel[0], texel[1], texel[2], texel[3]}, {}, false};
                } else {
                    shaded = render::shadePrimitive(frame, desc, x + 0.5, y + 0.5);
                }
                color = render::blend(desc.blend(), shaded, color);
            }
            while (path < segment.pathEnd) {
                color = applyPath(frame, path++, x, y, color);
            }
            target = color;
        }
    }
}

void CpuRaster::applyEffect(const render::EffectDraw &effect) {
    const hal::Rect crop = render::effectCrop(effect, width_, height_);
    if (crop.empty()) {
        return;
    }
    cropImage_.resize(static_cast<std::uint32_t>(crop.x1 - crop.x0),
                      static_cast<std::uint32_t>(crop.y1 - crop.y0));
    for (std::int32_t y = crop.y0; y < crop.y1; ++y) {
        for (std::int32_t x = crop.x0; x < crop.x1; ++x) {
            const auto &pixel =
                pixels_[static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x)];
            cropImage_.at(static_cast<std::uint32_t>(x - crop.x0),
                          static_cast<std::uint32_t>(y - crop.y0)) = {
                static_cast<float>(pixel.r), static_cast<float>(pixel.g),
                static_cast<float>(pixel.b), static_cast<float>(pixel.a)};
        }
    }
    render::blurCrop(cropImage_, blurredImage_, effect.sigma);
    for (std::int32_t y = crop.y0; y < crop.y1; ++y) {
        for (std::int32_t x = crop.x0; x < crop.x1; ++x) {
            const double mask = render::effectMask(effect, x + 0.5, y + 0.5);
            if (mask <= 0) {
                continue;
            }
            auto &pixel =
                pixels_[static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x)];
            const auto &blurred = blurredImage_.at(static_cast<std::uint32_t>(x - crop.x0),
                                                   static_cast<std::uint32_t>(y - crop.y0));
            pixel = {
                pixel.r + (blurred[0] - pixel.r) * mask, pixel.g + (blurred[1] - pixel.g) * mask,
                pixel.b + (blurred[2] - pixel.b) * mask, pixel.a + (blurred[3] - pixel.a) * mask};
        }
    }
}

void CpuRaster::render(const render::FrameView &frame, const hal::DamageRegion &damage,
                       math::PremultipliedColor clear) {
    bin(frame);
    rasterizePaths(frame);
    std::uint32_t analytic = 0;
    for (std::size_t i = 0; i < frame.primitives.size(); ++i) {
        const auto kind = frame.primitives[i].kind();
        if (kind != render::PrimitiveKind::wavetable3d &&
            kind != render::PrimitiveKind::formantCurve) {
            continue;
        }
        if (analytic == analyticImages_.size()) {
            analyticImages_.emplace_back();
        }
        bounds_[i].analytic = analytic;
        auto &image = analyticImages_[analytic++];
        if (kind == render::PrimitiveKind::wavetable3d) {
            render::rasterWavetable3d(frame, frame.primitives[i], image, width_, height_);
        } else {
            render::rasterFormantCurve(frame, frame.primitives[i], image, width_, height_);
        }
    }
    const hal::Rect viewport{0, 0, static_cast<std::int32_t>(width_),
                             static_cast<std::int32_t>(height_)};
    const auto primitiveCount = static_cast<std::uint32_t>(frame.primitives.size());
    if (frame.effects.empty()) {
        const Segment whole{&clear, 0, primitiveCount, 0, frame.paths.size()};
        damage.forEachRect([&](hal::Rect rect) {
            const hal::Rect clipped{std::max(rect.x0, viewport.x0), std::max(rect.y0, viewport.y0),
                                    std::min(rect.x1, viewport.x1), std::min(rect.y1, viewport.y1)};
            if (!clipped.empty()) {
                shadeRange(frame, clipped, whole);
            }
        });
        return;
    }
    Segment segment{&clear, 0, 0, 0, 0};
    for (std::size_t e = 0; e <= frame.effects.size(); ++e) {
        segment.primitiveEnd =
            e < frame.effects.size() ? frame.effects[e].insertAfter : primitiveCount;
        segment.pathEnd = segment.pathBegin;
        while (segment.pathEnd < frame.paths.size() &&
               (e == frame.effects.size() ||
                frame.paths[segment.pathEnd].insertAfter <= segment.primitiveEnd)) {
            ++segment.pathEnd;
        }
        shadeRange(frame, viewport, segment);
        if (e < frame.effects.size()) {
            applyEffect(frame.effects[e]);
        }
        segment = {nullptr, segment.primitiveEnd, segment.primitiveEnd, segment.pathEnd,
                   segment.pathEnd};
    }
}
}
