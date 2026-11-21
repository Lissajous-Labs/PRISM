#pragma once

#include "telemetry/TelemetryRing.h"
#include "widgets/GpuScope.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace prism::widgets {
class GpuNoiseEnvelope {
  public:
    explicit GpuNoiseEnvelope(std::uint32_t columns = 128)
        : columns_(columns), scope_(columns, 16384) {}
    [[nodiscard]] bool push(const float *samples, std::size_t count) noexcept {
        return audio_.push(samples, count);
    }
    bool step(double dt, double samplesPerPixel = 16, bool scrolling = true) noexcept;
    [[nodiscard]] bool pending() const noexcept { return audio_.available() != 0; }
    [[nodiscard]] render::TextureView strip() const noexcept { return scope_.strip(); }
    [[nodiscard]] render::TextureView rmsStrip() const noexcept { return scope_.rmsStrip(); }
    void draw(render::DrawList &list, render::RectF rect, std::uint32_t slot, std::uint32_t color,
              float thickness = 1) const {
        list.plotStrip(rect, slot, columns_, thickness, color, static_cast<float>(scroll_), 0.08f);
    }
    void draw(render::DrawList &list, render::RectF rect, std::uint32_t peakSlot,
              std::uint32_t rmsSlot, std::uint32_t color, float peakAlpha, float thickness = 1,
              float edgeFade = 0) const {
        const auto baseAlpha = static_cast<float>((color >> 24) & 0xFFu);
        const auto scaled = static_cast<std::uint32_t>(
            std::clamp(std::lround(baseAlpha * peakAlpha), 0l, 255l));
        const std::uint32_t peakColor = (color & 0x00FFFFFFu) | (scaled << 24);
        list.plotStrip(rect, peakSlot, columns_, thickness, peakColor, static_cast<float>(scroll_),
                       edgeFade);
        list.plotStrip(rect, rmsSlot, columns_, thickness, color, static_cast<float>(scroll_),
                       edgeFade);
    }

  private:
    std::uint32_t columns_;
    GpuScope scope_;
    telemetry::TelemetryRing<float, 16384> audio_;
    std::array<float, 4096> incoming_{};
    double scroll_ = 0;
    bool populated_ = false;
};
}
