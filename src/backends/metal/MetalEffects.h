#pragma once

#import <Metal/Metal.h>

#include "backends/metal/MetalState.h"
#include "hal/HalTypes.h"
#include "render/effects/Bloom.h"
#include "render/effects/EffectDraw.h"

#include <cstdint>
#include <vector>

namespace prism::backends {
class MetalEffects {
  public:
    void backdrop(MetalDeviceState &device, id<MTLCommandBuffer> commands, id<MTLTexture> color,
                  const render::EffectDraw &effect, hal::Extent extent);
    void bloom(MetalDeviceState &device, id<MTLCommandBuffer> commands, id<MTLTexture> scene,
               id<MTLTexture> output, const render::BloomSettings &settings);

  private:
    struct PooledTexture {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t slot;
        id<MTLTexture> texture;
    };
    std::vector<PooledTexture> pool_;
    id<MTLBuffer> taps_ = nil;

    id<MTLTexture> texture(MetalDeviceState &device, std::uint32_t width, std::uint32_t height,
                           std::uint32_t slot);
    id<MTLTexture> kawaseChain(MetalDeviceState &device, id<MTLCommandBuffer> commands,
                               id<MTLTexture> source, int levels, std::uint32_t slotBase);
};
}
