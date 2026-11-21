#pragma once

#include "render/DrawList.h"
#include "telemetry/FrameStatistics.h"

#include <cstdint>

namespace prism::studio {
struct HudSample {
    double hostMs;
    double gpuMs;
};

class GpuProfilerHud {
  public:
    explicit GpuProfilerHud(std::size_t window = 240) : host_(window), gpu_(window) {}

    void record(HudSample sample) noexcept {
        host_.add(sample.hostMs);
        gpu_.add(sample.gpuMs);
    }
    void draw(render::DrawList &list, render::RectF rect, double budgetMs);
    [[nodiscard]] telemetry::FrameStatistics &host() noexcept { return host_; }
    [[nodiscard]] telemetry::FrameStatistics &gpu() noexcept { return gpu_; }

  private:
    telemetry::FrameStatistics host_;
    telemetry::FrameStatistics gpu_;
};
}
