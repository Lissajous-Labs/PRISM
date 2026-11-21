#pragma once

#include "hal/DamageRegion.h"
#include "math/Color.h"
#include "render/FrameView.h"
#include "render/vector/PathTiler.h"

#include <cstdint>
#include <span>
#include <vector>

namespace prism::backends {
inline constexpr std::uint32_t rasterTileSize = 16;

class CpuRaster {
  public:
    void resize(std::uint32_t width, std::uint32_t height);
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

    void bin(const render::FrameView &frame);
    void rasterizePaths(const render::FrameView &frame);
    [[nodiscard]] math::PremultipliedColor applyPath(const render::FrameView &frame,
                                                     std::size_t path, std::int32_t x,
                                                     std::int32_t y,
                                                     math::PremultipliedColor color) const;
    void shadeRect(const render::FrameView &frame, hal::Rect rect, math::PremultipliedColor clear);
};
}
