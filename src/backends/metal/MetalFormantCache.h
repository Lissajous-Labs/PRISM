#pragma once

#include "backends/metal/MetalState.h"
#include "render/FormantCurve.h"

#include <array>

namespace prism::backends {
class MetalFormantCache {
  public:
    void prepare(MetalDeviceState &device, const render::FrameView &frame, id<MTLBuffer> primitives,
                 hal::Extent extent, bool present);
    void encode(MetalDeviceState &device, id<MTLCommandBuffer> commands, id<MTLBuffer> primitives,
                id<MTLBuffer> payload, id<MTLBuffer> transforms);
    [[nodiscard]] id<MTLTexture> texture(std::size_t slot) const noexcept {
        return slot < count_ ? entries_[slot].texture : nil;
    }

  private:
    struct Entry {
        id<MTLTexture> texture = nil;
        render::FormantPayload payload{};
        render::TransformRecord transform{};
        float dbRange = 0;
        std::uint32_t primitive = 0;
        bool dirty = true;
    };
    std::array<Entry, formantSlots> entries_{};
    std::size_t count_ = 0;
};
}
