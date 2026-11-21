#pragma once

#import <Metal/Metal.h>

#include "backends/metal/MetalState.h"
#include "hal/DamageRegion.h"
#include "render/FrameView.h"
#include "render/vector/TileGeometry.h"

#include <vector>

namespace prism::backends {
struct MetalPathBuffers {
    id<MTLBuffer> segments = nil;
    id<MTLBuffer> counts = nil;
    id<MTLBuffer> rowDeltas = nil;
    id<MTLBuffer> offsets = nil;
    id<MTLBuffer> backdrops = nil;
    id<MTLBuffer> cursors = nil;
    id<MTLBuffer> pieces = nil;
};

struct PathFineTargets {
    id<MTLTexture> color;
    id<MTLTexture> depth;
    id<MTLBuffer> transforms;
    id<MTLBuffer> clips;
    std::uint32_t maxDepth;
};

class MetalPathEngine {
  public:
    void prepare(MetalDeviceState &device, id<MTLCommandBuffer> commands,
                 const render::FrameView &frame, hal::Extent extent);
    void encodeFine(MetalDeviceState &device, id<MTLCommandBuffer> commands, std::size_t path,
                    const render::FrameView &frame, hal::Rect damage,
                    const PathFineTargets &targets);

  private:
    std::vector<MetalPathBuffers> buffers_;
    std::vector<float> segmentScratch_;
    render::TileGrid grid_{0, 0};
};
}
