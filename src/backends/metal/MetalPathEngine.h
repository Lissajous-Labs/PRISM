#pragma once

#import <Metal/Metal.h>

#include "backends/metal/MetalState.h"
#include "hal/DamageRegion.h"
#include "render/FrameView.h"
#include "render/vector/TileGeometry.h"

#include <span>
#include <vector>

namespace prism::backends {
struct MetalPathBatch {
    std::array<std::uint32_t, 16> uniforms;
    std::array<std::uint64_t, 10> buffers;
};

static_assert(sizeof(MetalPathBatch) == 144);

struct MetalPathBuffers {
    id<MTLBuffer> segments = nil;
    id<MTLBuffer> counts = nil;
    id<MTLBuffer> rowDeltas = nil;
    id<MTLBuffer> offsets = nil;
    id<MTLBuffer> scanState = nil;
    id<MTLBuffer> backdrops = nil;
    id<MTLBuffer> cursors = nil;
    id<MTLBuffer> pieces = nil;
    id<MTLBuffer> coverage = nil;
    std::uint32_t originX = 0;
    std::uint32_t originY = 0;
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;
};

class MetalPathEngine {
  public:
    void prepare(MetalDeviceState &device, id<MTLCommandBuffer> commands,
                 const render::FrameView &frame, hal::Extent extent, id<MTLBuffer> transforms,
                 id<MTLBuffer> clips);
    void composite(MetalDeviceState &device, id<MTLRenderCommandEncoder> encoder, std::size_t path);

  private:
    std::vector<MetalPathBuffers> buffers_;
    std::vector<MetalPathBatch> batchRecords_;
    id<MTLBuffer> batchBuffer_ = nil;
    render::TileGrid grid_{0, 0};
};
}
