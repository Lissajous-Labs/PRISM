#include "backends/metal/MetalWavetable3d.h"

#include <array>

namespace prism::backends {
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
                     const render::PrimitiveDesc &desc, std::uint32_t index) {
    [encoder setRenderPipelineState:device.wavetable];
    [encoder setVertexBytes:&index length:sizeof(index) atIndex:8];
    std::uint32_t mode = 0;
    [encoder setVertexBytes:&mode length:sizeof(mode) atIndex:9];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:72];
    mode = 1;
    [encoder setVertexBytes:&mode length:sizeof(mode) atIndex:9];
    const auto phases = static_cast<std::uint32_t>(desc.params[0]);
    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:12 * (phases - 1)
                         indexType:MTLIndexTypeUInt16
                       indexBuffer:device.wavetableIndices[phases == 128 ? 0 : 1]
                 indexBufferOffset:0
                     instanceCount:static_cast<NSUInteger>(desc.params[1])];
    mode = 2;
    [encoder setVertexBytes:&mode length:sizeof(mode) atIndex:9];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:72];
}
}
