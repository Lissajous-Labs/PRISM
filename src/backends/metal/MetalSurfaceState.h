#pragma once

#import <Metal/Metal.h>

#include "backends/metal/MetalEffects.h"
#include "backends/metal/MetalFormantCache.h"
#include "backends/metal/MetalHalftoneCache.h"
#include "backends/metal/MetalPathEngine.h"
#include "backends/metal/MetalShadowCache.h"
#include "backends/metal/MetalState.h"
#include "render/RenderSurface.h"
#include "render/UberBatches.h"
#include "render/Wavetable3d.h"

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
    float origin[2];
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
    MetalFormantCache formants;
    MetalHalftoneCache halftones;
    MetalShadowCache shadows;
    std::vector<std::pair<std::uint32_t, render::WavetableLine>> wavetableLines;
    [[nodiscard]] const render::WavetableLine *wavetableLine(std::uint32_t index) const noexcept {
        for (const auto &entry : wavetableLines) {
            if (entry.first == index) {
                return &entry.second;
            }
        }
        return nullptr;
    }
    id<MTLTexture> output = nil;
    bool bloomActive = false;
    std::vector<std::uint32_t> indexScratch;
    std::vector<float> texelScratch;
    struct ScratchRange {
        std::uint32_t opaque;
        std::uint32_t ordered;
        std::uint32_t end;
    };
    std::vector<hal::Rect> primitiveBounds;
    std::vector<std::uint32_t> scratchIndices;
    std::vector<ScratchRange> scratchRanges;
    id<MTLBuffer> scratchIndexBuffer = nil;
    std::vector<hal::Rect> damageRects;
    std::vector<std::array<std::int32_t, 2>> scratchSlots;
    id<MTLTexture> scratchColor = nil;
    id<MTLTexture> scratchDepth = nil;
    bool lost = false;
    bool earlyDepth = true;
    bool counting = false;
    std::uint32_t fragments = 0;
    double gpuMilliseconds = 0;
    std::uint32_t drawCalls = 0;
    std::size_t uploadBytes = 0;
    bool submitted = false;

    void createTargets();
    void releaseTargets();
    void uploadFrame(const render::FrameView &view);
    [[nodiscard]] id<MTLTexture> presentedTexture() const noexcept {
        return bloomActive ? output : color;
    }
    void encodeFrame(id<MTLCommandBuffer> commands, const render::FrameRequest &request);

  private:
    void bindUber(id<MTLRenderCommandEncoder> encoder, const FrameUniforms &uniforms);
    id<MTLRenderCommandEncoder> beginPass(id<MTLCommandBuffer> commands,
                                          MTLRenderPassDescriptor *pass,
                                          const FrameUniforms &uniforms);
    void drawOrdered(id<MTLRenderCommandEncoder> encoder, std::uint32_t from, std::uint32_t to);
    void encodeScratch(id<MTLCommandBuffer> commands, FrameUniforms uniforms, const float *clear);
};
}
