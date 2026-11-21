#pragma once

#import <Metal/Metal.h>

#include "render/UberBatches.h"

#include <array>
#include <memory>

namespace prism::backends {
inline constexpr std::size_t uberPipelineCount = 6;
inline constexpr std::size_t textureSlots = 16;
inline constexpr std::size_t formantSlots = 8;

struct MetalDeviceState {
    id<MTLDevice> device = nil;
    id<MTLLibrary> library = nil;
    std::array<id<MTLRenderPipelineState>, uberPipelineCount> pipelines{};
    id<MTLRenderPipelineState> opaque = nil;
    id<MTLRenderPipelineState> counting = nil;
    id<MTLRenderPipelineState> countingCore = nil;
    id<MTLRenderPipelineState> clear = nil;
    id<MTLRenderPipelineState> wavetable = nil;
    std::array<id<MTLBuffer>, 2> wavetableIndices{};
    id<MTLDepthStencilState> depthWrite = nil;
    id<MTLDepthStencilState> depthTest = nil;
    id<MTLDepthStencilState> depthReset = nil;
    id<MTLComputePipelineState> reflect = nil;
    id<MTLComputePipelineState> present = nil;
    id<MTLComputePipelineState> presentDrawable = nil;
    id<MTLComputePipelineState> pathBinCount = nil;
    id<MTLComputePipelineState> pathScan = nil;
    id<MTLComputePipelineState> pathBackdrop = nil;
    id<MTLComputePipelineState> pathScatter = nil;
    id<MTLComputePipelineState> pathFine = nil;
    id<MTLComputePipelineState> blurPaired = nil;
    id<MTLComputePipelineState> blurShared = nil;
    id<MTLComputePipelineState> kawaseDown = nil;
    id<MTLComputePipelineState> kawaseUp = nil;
    id<MTLComputePipelineState> mixTextures = nil;
    id<MTLComputePipelineState> effectComposite = nil;
    id<MTLComputePipelineState> bloomPrefilter = nil;
    id<MTLComputePipelineState> bloomComposite = nil;
    id<MTLComputePipelineState> shadowProbe = nil;
    id<MTLComputePipelineState> formantColumns = nil;
    id<MTLComputePipelineState> halftoneCells = nil;
    id<MTLTexture> emptyTexture = nil;
    bool programmableBlending = false;
};

[[nodiscard]] std::shared_ptr<MetalDeviceState> createMetalDeviceState();
}
