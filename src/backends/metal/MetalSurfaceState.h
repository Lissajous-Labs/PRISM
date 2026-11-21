#pragma once

#import <Metal/Metal.h>

#include "backends/metal/MetalEffects.h"
#include "backends/metal/MetalPathEngine.h"
#include "backends/metal/MetalState.h"
#include "render/RenderSurface.h"
#include "render/UberBatches.h"

#include <array>
#include <memory>
#include <vector>

namespace prism::backends {
struct FrameUniforms {
    float viewport[2];
    std::uint32_t maxDepth;
    std::uint32_t textureCount;
    std::uint32_t countFragments;
    std::uint32_t gradientRows;
    std::uint32_t reserved[2];
};

struct MetalSurfaceState {
    std::shared_ptr<MetalDeviceState> device;
    hal::Extent extent;
    id<MTLCommandQueue> queue = nil;
    id<MTLTexture> color = nil;
    id<MTLTexture> depth = nil;
    id<MTLBuffer> primitives = nil;
    id<MTLBuffer> payload = nil;
    id<MTLBuffer> transforms = nil;
    id<MTLBuffer> clips = nil;
    id<MTLBuffer> indices = nil;
    id<MTLBuffer> counter = nil;
    id<MTLBuffer> readback = nil;
    id<MTLTexture> gradients = nil;
    std::array<id<MTLTexture>, textureSlots> textures{};
    std::array<render::TextureView, textureSlots> uploadedTextures{};
    render::UberBatches batches;
    MetalPathEngine paths;
    MetalEffects effects;
    id<MTLTexture> output = nil;
    bool bloomActive = false;
    std::vector<std::uint32_t> indexScratch;
    std::vector<float> texelScratch;
    bool lost = false;
    bool earlyDepth = true;
    bool counting = false;
    std::uint32_t fragments = 0;
    double gpuMilliseconds = 0;

    void createTargets();
    void releaseTargets();
    void uploadFrame(const render::FrameView &view);
    [[nodiscard]] id<MTLTexture> presentedTexture() const noexcept {
        return bloomActive ? output : color;
    }
    void encodeFrame(id<MTLCommandBuffer> commands, const render::FrameRequest &request);

  private:
    void bindUber(id<MTLRenderCommandEncoder> encoder, const FrameUniforms &uniforms);
    id<MTLRenderCommandEncoder> beginRect(id<MTLCommandBuffer> commands,
                                          MTLRenderPassDescriptor *pass, hal::Rect rect,
                                          const FrameUniforms &uniforms);
    void drawOrdered(id<MTLRenderCommandEncoder> encoder, std::uint32_t from, std::uint32_t to);
};
}
