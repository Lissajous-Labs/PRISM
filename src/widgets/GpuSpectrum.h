#pragma once

#include "render/DrawList.h"
#include "render/FrameView.h"
#include "telemetry/SpectrumEstimator.h"

#include <cstdint>
#include <span>
#include <vector>

namespace prism::widgets {
class GpuSpectrum {
  public:
    GpuSpectrum(std::uint32_t columns, double sampleRate, double tiltDbPerOctave = 4.5);

    void push(std::span<const float> samples) noexcept { estimator_.push(samples); }
    bool analyze(std::span<const float> block, double dt) noexcept;
    void buildStrip(float minDb, float maxDb);
    void appendWaterfallRow(float minDb, float maxDb);

    [[nodiscard]] std::span<const float> levels() const noexcept { return levels_; }
    [[nodiscard]] std::span<const float> peaks() const noexcept { return peaks_; }
    [[nodiscard]] double columnFrequency(std::uint32_t column) const noexcept {
        return estimator_.frequencies()[column];
    }
    [[nodiscard]] const telemetry::SpectrumEstimator &estimator() const noexcept {
        return estimator_;
    }
    [[nodiscard]] render::TextureView strip() const noexcept { return {columns_, 1, 2, strip_}; }
    [[nodiscard]] render::TextureView waterfall() const noexcept {
        return {columns_, waterfallRows, 1, waterfall_};
    }
    [[nodiscard]] std::uint32_t waterfallOffset() const noexcept { return waterfallOffset_; }

    void draw(render::DrawList &list, render::RectF rect, std::uint32_t color,
              float strokeWidth = 1.5f);
    void drawWaterfall(render::DrawList &list, render::RectF rect, std::uint32_t slot,
                       std::uint32_t tint) const;

    static constexpr std::uint32_t waterfallRows = 128;

  private:
    std::uint32_t columns_;
    telemetry::SpectrumEstimator estimator_;
    std::vector<float> levels_;
    std::vector<float> peaks_;
    std::vector<float> units_;
    std::vector<float> unitSlopes_;
    std::vector<float> heights_;
    std::vector<float> slopes_;
    std::vector<float> strip_;
    std::vector<float> waterfall_;
    std::vector<double> positions_;
    std::vector<double> unitValues_;
    std::vector<double> unitDerivatives_;
    std::uint32_t waterfallOffset_ = 0;
    float range_ = 1;
};
}
