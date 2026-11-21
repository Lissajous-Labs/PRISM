#pragma once

#include "render/DrawList.h"
#include "render/FrameView.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace prism::widgets {
class PhosphorBuffer {
  public:
    PhosphorBuffer(std::uint32_t width, std::uint32_t height)
        : width_(width), height_(height),
          texels_(static_cast<std::size_t>(width) * height * 4, 0.0f) {}

    void decay(double dt, double tau) noexcept;
    void deposit(std::span<const float> strip, float intensity) noexcept;
    [[nodiscard]] render::TextureView texture() const noexcept {
        return {width_, height_, 4, texels_};
    }
    [[nodiscard]] float at(std::uint32_t x, std::uint32_t y) const noexcept {
        return texels_[(static_cast<std::size_t>(y) * width_ + x) * 4 + 3];
    }

  private:
    std::uint32_t width_;
    std::uint32_t height_;
    std::vector<float> texels_;
};

class GpuScope {
  public:
    GpuScope(std::uint32_t columns, std::size_t capacity);

    void append(std::span<const float> samples) noexcept;
    void setTrigger(float level, float hysteresis) noexcept {
        level_ = level;
        hysteresis_ = hysteresis;
    }
    bool build(double samplesPerPixel) noexcept;

    [[nodiscard]] render::TextureView strip() const noexcept { return {columns_, 1, 2, strip_}; }
    [[nodiscard]] render::TextureView rmsStrip() const noexcept {
        return {columns_, 1, 2, rmsStrip_};
    }
    [[nodiscard]] std::span<const float> bands() const noexcept { return strip_; }
    [[nodiscard]] std::optional<std::uint64_t> triggerSample() const noexcept { return trigger_; }
    [[nodiscard]] float sampleAt(std::uint64_t absolute) const noexcept;

    void draw(render::DrawList &list, render::RectF rect, std::uint32_t textureSlot,
              float thickness, std::uint32_t color) const {
        list.plotStrip(rect, textureSlot, columns_, thickness, color);
    }

  private:
    std::uint32_t columns_;
    std::vector<float> history_;
    std::uint64_t written_ = 0;
    std::vector<float> strip_;
    std::vector<float> rmsStrip_;
    float level_ = 0;
    float hysteresis_ = 0.05f;
    std::optional<std::uint64_t> trigger_;

    [[nodiscard]] float interpolate(double position) const noexcept;
};
}
