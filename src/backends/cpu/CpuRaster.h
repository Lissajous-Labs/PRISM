#pragma once

#include "hal/DamageRegion.h"
#include "math/Color.h"
#include "render/FrameView.h"
#include "render/effects/BackdropBlur.h"
#include "render/vector/PathTiler.h"

#include <cstdint>
#include <span>
#include <vector>

namespace prism::backends {
inline constexpr std::uint32_t rasterTileSize = 16;

class CpuRaster {
  public:
    void resize(std::uint32_t width, std::uint32_t height);
    void reserveFrame(std::size_t primitives, std::size_t tileEntries) {
        bounds_.reserve(primitives);
        tileEntries_.reserve(tileEntries);
    }
    void render(const render::FrameView &frame, const hal::DamageRegion &damage,
                math::PremultipliedColor clear);

    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] std::span<const math::PremultipliedColor> pixels() const noexcept {
        return pixels_;
    }

  private:
    struct Bounds {
        std::int32_t x0;
        std::int32_t y0;
        std::int32_t x1;
        std::int32_t y1;
        std::uint32_t analytic;
    };

    struct Segment {
        const math::PremultipliedColor *clear;
        std::uint32_t primitiveBegin;
        std::uint32_t primitiveEnd;
        std::size_t pathBegin;
        std::size_t pathEnd;
    };

    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t tilesX_ = 0;
    std::uint32_t tilesY_ = 0;
    std::vector<math::PremultipliedColor> pixels_;
    std::vector<Bounds> bounds_;
    std::vector<std::uint32_t> tileCounts_;
    std::vector<std::uint32_t> tileOffsets_;
    std::vector<std::uint32_t> tileEntries_;
    render::PathTiler tiler_;
    std::vector<std::vector<float>> pathCoverage_;
    render::FloatImage cropImage_;
    render::FloatImage blurredImage_;
    std::vector<render::FloatImage> analyticImages_;

    void bin(const render::FrameView &frame);
    void rasterizePaths(const render::FrameView &frame);
    [[nodiscard]] math::PremultipliedColor applyPath(const render::FrameView &frame,
                                                     std::size_t path, std::int32_t x,
                                                     std::int32_t y,
                                                     math::PremultipliedColor color) const;
    void shadeRange(const render::FrameView &frame, hal::Rect rect, const Segment &segment);
    void applyEffect(const render::EffectDraw &effect);
};
}
