#pragma once

#include "math/WavetableWarp.h"
#include "render/FrameView.h"

#include <cstdint>
#include <memory>

namespace prism::backends {
struct MetalDeviceState;
class MetalSpectrogram {
  public:
    MetalSpectrogram(std::shared_ptr<MetalDeviceState> device, math::WavetableView table,
                     std::uint32_t rows);
    ~MetalSpectrogram();
    [[nodiscard]] bool prepare(const math::WavetableWarp &warp = {}, float tilt = 0.25f,
                               std::uint32_t firstFrame = 0,
                               std::uint32_t count = UINT32_MAX) noexcept;
    [[nodiscard]] bool dispatch();
    [[nodiscard]] render::TextureView field() const noexcept;
    [[nodiscard]] render::TextureView integral() const noexcept;
    [[nodiscard]] double lastGpuMilliseconds() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
