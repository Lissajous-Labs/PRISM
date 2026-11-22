#pragma once

#include "backends/metal/MetalState.h"
#include "render/FrameView.h"

#include <array>
#include <cstdint>
#include <vector>

namespace prism::backends {
class MetalHalftoneCache {
  public:
    void prepare(MetalDeviceState &device, const render::FrameView &frame, id<MTLBuffer> primitives,
                 id<MTLBuffer> payload, bool present);
    void encode(MetalDeviceState &device, id<MTLCommandBuffer> commands, id<MTLBuffer> primitives,
                id<MTLBuffer> payload, id<MTLTexture> gradients,
                const std::array<id<MTLTexture>, textureSlots> &textures);
    [[nodiscard]] id<MTLBuffer> cells() const noexcept { return cells_; }

  private:
    struct Job {
        std::uint32_t primitive;
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t base;
        std::uint32_t field;
        std::uint32_t noise;
    };
    std::vector<Job> jobs_;
    id<MTLBuffer> cells_ = nil;
};
}
