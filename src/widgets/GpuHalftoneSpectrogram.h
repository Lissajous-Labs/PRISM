#pragma once

#include "math/WavetableWarp.h"
#include "telemetry/Fft.h"
#include "widgets/HalftoneStyle.h"

#include <complex>
#include <cstdint>
#include <span>
#include <vector>

namespace prism::widgets {
class GpuHalftoneSpectrogram {
  public:
    GpuHalftoneSpectrogram(std::uint32_t frames = 256, std::uint32_t period = 2048,
                           std::uint32_t rows = 64);
    [[nodiscard]] bool build(math::WavetableView table, const math::WavetableWarp &warp = {},
                             double tilt = 0.25, std::uint32_t firstFrame = 0,
                             std::uint32_t frameCount = UINT32_MAX);
    [[nodiscard]] render::TextureView field() const noexcept {
        return {frames_, rows_, 1, field_, hal::PixelFormat::r16Float};
    }
    [[nodiscard]] render::TextureView integral() const noexcept {
        return {frames_ + 1, rows_ + 1, 2, integral_, hal::PixelFormat::rg32Float};
    }
    void draw(render::DrawList &list, render::RectF rect, std::uint32_t fieldSlot,
              std::uint32_t paletteRow, std::uint32_t noiseSlot, float position,
              unsigned unison = 1, const HalftoneStyle &style = {}) const;

  private:
    std::uint32_t frames_, period_, rows_;
    telemetry::Fft fft_;
    std::vector<double> input_;
    std::vector<std::complex<double>> spectrum_;
    std::vector<float> field_;
    std::vector<float> integral_;
    std::vector<double> edges_;
    std::vector<double> tiltWeights_;
};
}
