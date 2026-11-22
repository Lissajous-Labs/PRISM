#pragma once

#include "backends/metal/MetalState.h"
#include "render/FrameView.h"

#include <cstdint>
#include <vector>

namespace prism::backends {
struct ShadowSprite {
    std::uint32_t offset;
    std::uint32_t width;
    std::uint32_t height;
    float step;
    float half[2];
    float radius;
    float sigma;
};

class MetalShadowCache {
  public:
    void prepare(MetalDeviceState &device, const render::FrameView &frame,
                 id<MTLBuffer> primitives);
    void encode(MetalDeviceState &device, id<MTLCommandBuffer> commands);
    [[nodiscard]] id<MTLBuffer> sprites() const noexcept { return sprites_; }
    [[nodiscard]] id<MTLBuffer> table() const noexcept { return table_; }

  private:
    std::vector<ShadowSprite> entries_;
    std::vector<std::uint32_t> pending_;
    std::vector<float> sampleSteps_;
    std::uint32_t texels_ = 0;
    id<MTLBuffer> sprites_ = nil;
    id<MTLBuffer> table_ = nil;
};
}
