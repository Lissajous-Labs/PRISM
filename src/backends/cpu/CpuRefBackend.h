#pragma once

#include "backends/cpu/CpuRaster.h"
#include "render/RenderSurface.h"

#include <memory>

namespace prism::backends {
class CpuRefSurface final : public render::RenderSurface {
  public:
    explicit CpuRefSurface(hal::SurfaceDesc desc);

    [[nodiscard]] hal::Extent extent() const noexcept override { return extent_; }
    void resize(hal::Extent extent) override;
    void loseDevice() noexcept override { lost_ = true; }
    [[nodiscard]] bool rebuild() override;
    [[nodiscard]] bool lost() const noexcept override { return lost_; }
    [[nodiscard]] bool render(const render::FrameRequest &request) override;
    [[nodiscard]] bool readback(std::span<float> rgba) override;

  private:
    hal::Extent extent_;
    CpuRaster raster_;
    bool lost_ = false;
};

class CpuRefDevice final : public render::RenderDevice {
  public:
    [[nodiscard]] hal::BackendKind kind() const noexcept override {
        return hal::BackendKind::cpuReference;
    }
    [[nodiscard]] std::unique_ptr<render::RenderSurface>
    createRenderSurface(const hal::SurfaceDesc &desc) override {
        return std::make_unique<CpuRefSurface>(desc);
    }
    static std::unique_ptr<hal::BackendDevice> create() { return std::make_unique<CpuRefDevice>(); }
};
}
