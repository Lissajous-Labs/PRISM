#include "backends/metal/MetalWavetable3d.h"

#include <algorithm>
#include <array>
#include <limits>

namespace prism::backends {
namespace {
struct LineData {
    std::uint32_t count;
    float radius;
    float bounds[4];
    float points[512];
    float depth[256];
    float padding[2];
    float chunks[64];
};
static_assert(sizeof(LineData) == 3360);
}
bool buildWavetableIndices(MetalDeviceState &device) {
    std::array<std::uint16_t, 12 * 255> indices{};
    for (std::size_t resolution = 0; resolution < 2; ++resolution) {
        const std::uint16_t phases = resolution == 0 ? 128 : 256;
        std::size_t count = 0;
        for (std::uint16_t layer = 0; layer < 2; ++layer) {
            for (std::uint16_t i = 0; i + 1 < phases; ++i) {
                const auto base = static_cast<std::uint16_t>(layer * 2 * phases + 2 * i);
                for (const std::uint16_t offset : {0, 1, 2, 1, 3, 2}) {
                    indices[count++] = base + offset;
                }
            }
        }
        device.wavetableIndices[resolution] =
            [device.device newBufferWithBytes:indices.data()
                                       length:count * sizeof(std::uint16_t)
                                      options:MTLResourceStorageModeShared];
        if (!device.wavetableIndices[resolution]) {
            return false;
        }
    }
    return true;
}
void drawWavetable3d(id<MTLRenderCommandEncoder> encoder, const MetalDeviceState &device,
                     const render::PrimitiveDesc &desc, std::uint32_t index,
                     const render::WavetableLine *line) {
    LineData data{};
    if (line != nullptr) {
        data.count = line->count;
        data.radius = line->radius;
        float minX = std::numeric_limits<float>::infinity(), minY = minX;
        float maxX = -minX, maxY = -minX;
        for (std::uint32_t i = 0; i < line->count; ++i) {
            const auto x = static_cast<float>(line->points[i].x);
            const auto y = static_cast<float>(line->points[i].y);
            data.points[2 * i] = x;
            data.points[2 * i + 1] = y;
            data.depth[i] = line->depth[i];
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
        const float margin = line->radius + 1;
        data.bounds[0] = minX - margin;
        data.bounds[1] = minY - margin;
        data.bounds[2] = maxX + margin;
        data.bounds[3] = maxY + margin;
        for (std::uint32_t c = 0; c < 16; ++c) {
            float lowX = std::numeric_limits<float>::infinity(), lowY = lowX;
            float highX = -lowX, highY = -lowX;
            const auto last = std::min(c * 16 + 16, line->count == 0 ? 0 : line->count - 1);
            for (std::uint32_t i = c * 16; i <= last && c * 16 < last; ++i) {
                lowX = std::min(lowX, data.points[2 * i]);
                lowY = std::min(lowY, data.points[2 * i + 1]);
                highX = std::max(highX, data.points[2 * i]);
                highY = std::max(highY, data.points[2 * i + 1]);
            }
            data.chunks[4 * c] = lowX - line->radius;
            data.chunks[4 * c + 1] = lowY - line->radius;
            data.chunks[4 * c + 2] = highX + line->radius;
            data.chunks[4 * c + 3] = highY + line->radius;
        }
    }
    [encoder setRenderPipelineState:device.wavetable];
    [encoder setVertexBytes:&index length:sizeof(index) atIndex:8];
    [encoder setVertexBytes:&data length:sizeof(data) atIndex:10];
    [encoder setFragmentBytes:&data length:sizeof(data) atIndex:10];
    std::uint32_t mode = 0;
    [encoder setVertexBytes:&mode length:sizeof(mode) atIndex:9];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:72];
    const auto phases = static_cast<std::uint32_t>(desc.params[0]);
    const auto instances = static_cast<std::uint32_t>(desc.params[1]);
    const auto slices = [&](std::uint32_t first, std::uint32_t count) {
        if (count == 0) {
            return;
        }
        std::uint32_t sliceMode = 1;
        [encoder setVertexBytes:&sliceMode length:sizeof(sliceMode) atIndex:9];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:12 * (phases - 1)
                             indexType:MTLIndexTypeUInt16
                           indexBuffer:device.wavetableIndices[phases == 128 ? 0 : 1]
                     indexBufferOffset:0
                         instanceCount:count
                            baseVertex:0
                          baseInstance:first];
    };
    const std::uint32_t split = line != nullptr ? std::min(line->slice + 1, instances) : instances;
    slices(0, split);
    if (line != nullptr) {
        mode = 3;
        [encoder setVertexBytes:&mode length:sizeof(mode) atIndex:9];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    }
    slices(split, instances - split);
    mode = 2;
    [encoder setVertexBytes:&mode length:sizeof(mode) atIndex:9];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:72];
}
}
