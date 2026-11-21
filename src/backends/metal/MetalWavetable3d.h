#pragma once

#include "backends/metal/MetalState.h"
#include "render/PrimitiveDesc.h"

namespace prism::backends {
[[nodiscard]] bool buildWavetableIndices(MetalDeviceState &device);
void drawWavetable3d(id<MTLRenderCommandEncoder> encoder, const MetalDeviceState &device,
                     const render::PrimitiveDesc &desc, std::uint32_t index);
}
