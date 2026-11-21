#pragma once

#import <Metal/Metal.h>

#include "render/UberBatches.h"

#include <array>
#include <memory>

namespace prism::backends {
inline constexpr std::size_t uberPipelineCount = 6;
inline constexpr std::size_t textureSlots = 8;

struct MetalDeviceState {
    id<MTLDevice> device = nil;
    id<MTLLibrary> library = nil;
    std::array<id<MTLRenderPipelineState>, uberPipelineCount> pipelines{};
    id<MTLRenderPipelineState> opaque = nil;
    id<MTLRenderPipelineState> counting = nil;
    id<MTLRenderPipelineState> countingCore = nil;
    id<MTLRenderPipelineState> clear = nil;
    id<MTLDepthStencilState> depthWrite = nil;
    id<MTLDepthStencilState> depthTest = nil;
    id<MTLDepthStencilState> depthReset = nil;
    id<MTLComputePipelineState> reflect = nil;
    id<MTLComputePipelineState> present = nil;
    id<MTLTexture> emptyTexture = nil;
    bool programmableBlending = false;
};

[[nodiscard]] std::shared_ptr<MetalDeviceState> createMetalDeviceState();
}
