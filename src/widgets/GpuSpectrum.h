#pragma once

#include "render/DrawList.h"
#include "render/FrameView.h"
#include "telemetry/Fft.h"

#include <complex>
#include <cstdint>
#include <span>
#include <vector>

namespace prism::widgets {
inline constexpr std::size_t spectrumSize = 4096;

class GpuSpectrum {
  public:
    GpuSpectrum(std::uint32_t columns, double sampleRate, float fallRate = 24,
                float floorDb = -120);

    void analyze(std::span<const float> block, double dt);
    void buildStrip(float minDb, float maxDb);
    void appendWaterfallRow(float minDb, float maxDb);

    [[nodiscard]] std::span<const float> levels() const noexcept { return levels_; }
    [[nodiscard]] double columnFrequency(std::uint32_t column) const noexcept;
    [[nodiscard]] render::TextureView strip() const noexcept { return {columns_, 1, 2, strip_}; }
    [[nodiscard]] render::TextureView waterfall() const noexcept {
        return {columns_, waterfallRows, 1, waterfall_};
    }
    [[nodiscard]] std::uint32_t waterfallOffset() const noexcept { return waterfallOffset_; }

    void draw(render::DrawList &list, render::RectF rect, std::uint32_t stripSlot,
              std::uint32_t gradientRow, std::uint32_t lineColor, std::uint32_t fillColor) const;
    void drawWaterfall(render::DrawList &list, render::RectF rect, std::uint32_t slot,
                       std::uint32_t tint) const;

    static constexpr std::uint32_t waterfallRows = 128;

  private:
    std::uint32_t columns_;
    double sampleRate_;
    float fallRate_;
    float floorDb_;
    telemetry::Fft fft_;
    std::vector<double> window_;
    std::vector<double> input_;
    std::vector<std::complex<double>> spectrum_;
    std::vector<float> levels_;
    std::vector<float> instant_;
    std::vector<float> strip_;
    std::vector<float> waterfall_;
    std::uint32_t waterfallOffset_ = 0;
};
}
