#include "backends/metal/MetalBackend.h"
#include "backends/metal/MetalState.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <vector>

namespace prism::backends {
namespace {
struct FrameUniforms {
    float viewport[2];
    std::uint32_t maxDepth;
    std::uint32_t textureCount;
    std::uint32_t countFragments;
    std::uint32_t gradientRows;
    std::uint32_t reserved[2];
};

float halfToFloat(std::uint16_t half) noexcept {
    const std::uint32_t sign = (half & 0x8000u) << 16;
    const std::uint32_t exponent = (half >> 10) & 0x1Fu;
    const std::uint32_t mantissa = half & 0x3FFu;
    if (exponent == 0) {
        const float value = static_cast<float>(mantissa) * 0x1p-24f;
        return sign ? -value : value;
    }
    if (exponent == 31) {
        return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13));
    }
    return std::bit_cast<float>(sign | ((exponent + 112) << 23) | (mantissa << 13));
}

void upload(id<MTLDevice> device, id<MTLBuffer> __strong &buffer, const void *data,
            std::size_t bytes) {
    const std::size_t needed = std::max<std::size_t>(bytes, 64);
    if (buffer == nil || buffer.length < needed) {
        buffer = [device newBufferWithLength:std::bit_ceil(needed)
                                     options:MTLResourceStorageModeShared];
    }
    if (bytes != 0 && data != nullptr) {
        std::memcpy(buffer.contents, data, bytes);
    }
}

id<MTLTexture> uploadTexture(id<MTLDevice> device, id<MTLTexture> cached, std::uint32_t width,
                             std::uint32_t height, std::uint32_t channels,
                             std::span<const float> texels, std::vector<float> &scratch) {
    if (width == 0 || height == 0 ||
        texels.size() < static_cast<std::size_t>(width) * height * channels) {
        return nil;
    }
    if (cached == nil || cached.width != width || cached.height != height) {
        MTLTextureDescriptor *descriptor =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                               width:width
                                                              height:height
                                                           mipmapped:NO];
        cached = [device newTextureWithDescriptor:descriptor];
    }
    scratch.assign(static_cast<std::size_t>(width) * height * 4, 0.0f);
    for (std::size_t texel = 0; texel < static_cast<std::size_t>(width) * height; ++texel) {
        for (std::uint32_t c = 0; c < 4; ++c) {
            scratch[texel * 4 + c] =
                c < channels ? texels[texel * channels + c] : (c == 3 ? 1.0f : 0.0f);
        }
    }
    [cached replaceRegion:MTLRegionMake2D(0, 0, width, height)
              mipmapLevel:0
                withBytes:scratch.data()
              bytesPerRow:width * 16];
    return cached;
}
}

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
    render::UberBatches batches;
    std::vector<std::uint32_t> indexScratch;
    std::vector<float> texelScratch;
    bool lost = false;
    bool earlyDepth = true;
    bool counting = false;
    std::uint32_t fragments = 0;

    void createTargets() {
        queue = [device->device newCommandQueue];
        MTLTextureDescriptor *colorDescriptor =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                               width:extent.width
                                                              height:extent.height
                                                           mipmapped:NO];
        colorDescriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        colorDescriptor.storageMode = MTLStorageModePrivate;
        color = [device->device newTextureWithDescriptor:colorDescriptor];
        MTLTextureDescriptor *depthDescriptor =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                               width:extent.width
                                                              height:extent.height
                                                           mipmapped:NO];
        depthDescriptor.usage = MTLTextureUsageRenderTarget;
        depthDescriptor.storageMode = MTLStorageModePrivate;
        depth = [device->device newTextureWithDescriptor:depthDescriptor];
        counter = [device->device newBufferWithLength:sizeof(std::uint32_t)
                                              options:MTLResourceStorageModeShared];
    }

    void releaseTargets() {
        queue = nil;
        color = nil;
        depth = nil;
    }

    void uploadFrame(const render::FrameView &view) {
        id<MTLDevice> gpu = device->device;
        upload(gpu, primitives, view.primitives.data(), view.primitives.size_bytes());
        upload(gpu, payload, view.payload.data(), view.payload.size_bytes());
        upload(gpu, transforms, view.transforms.data(), view.transforms.size_bytes());
        upload(gpu, clips, view.clips.data(), view.clips.size_bytes());
        batches.build(view.primitives, earlyDepth);
        indexScratch.assign(batches.opaqueIndices().begin(), batches.opaqueIndices().end());
        indexScratch.insert(indexScratch.end(), batches.ordered().begin(), batches.ordered().end());
        upload(gpu, indices, indexScratch.data(), indexScratch.size() * sizeof(std::uint32_t));
        const auto rows =
            static_cast<std::uint32_t>(view.gradientRows.size() / (render::gradientRowTexels * 4));
        gradients = rows ? uploadTexture(gpu, gradients, render::gradientRowTexels, rows, 4,
                                         view.gradientRows, texelScratch)
                         : nil;
        for (std::size_t slot = 0; slot < textureSlots; ++slot) {
            if (slot < view.textures.size()) {
                const auto &texture = view.textures[slot];
                textures[slot] = uploadTexture(gpu, textures[slot], texture.width, texture.height,
                                               texture.channels, texture.texels, texelScratch);
            } else {
                textures[slot] = nil;
            }
        }
    }

    void bindUber(id<MTLRenderCommandEncoder> encoder, const FrameUniforms &uniforms) {
        [encoder setVertexBuffer:primitives offset:0 atIndex:0];
        [encoder setVertexBuffer:payload offset:0 atIndex:1];
        [encoder setVertexBuffer:transforms offset:0 atIndex:2];
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
        [encoder setVertexBuffer:indices offset:0 atIndex:5];
        [encoder setFragmentBuffer:primitives offset:0 atIndex:0];
        [encoder setFragmentBuffer:payload offset:0 atIndex:1];
        [encoder setFragmentBuffer:transforms offset:0 atIndex:2];
        [encoder setFragmentBuffer:clips offset:0 atIndex:3];
        [encoder setFragmentBuffer:counter offset:0 atIndex:6];
        [encoder setFragmentTexture:(gradients ? gradients : device->emptyTexture) atIndex:0];
        for (std::size_t slot = 0; slot < textureSlots; ++slot) {
            [encoder setFragmentTexture:(textures[slot] ? textures[slot] : device->emptyTexture)
                                atIndex:slot + 1];
        }
    }

    void encodeRect(id<MTLRenderCommandEncoder> encoder, hal::Rect rect,
                    const render::FrameRequest &request, const FrameUniforms &uniforms) {
        [encoder setScissorRect:MTLScissorRect{static_cast<NSUInteger>(rect.x0),
                                               static_cast<NSUInteger>(rect.y0),
                                               static_cast<NSUInteger>(rect.x1 - rect.x0),
                                               static_cast<NSUInteger>(rect.y1 - rect.y0)}];
        const float clear[4] = {
            static_cast<float>(request.clear.r), static_cast<float>(request.clear.g),
            static_cast<float>(request.clear.b), static_cast<float>(request.clear.a)};
        [encoder setRenderPipelineState:device->clear];
        [encoder setDepthStencilState:device->depthReset];
        [encoder setFragmentBytes:clear length:sizeof(clear) atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        bindUber(encoder, uniforms);
        const auto opaqueCount = static_cast<NSUInteger>(batches.opaqueIndices().size());
        if (opaqueCount != 0) {
            [encoder setRenderPipelineState:counting ? device->countingCore : device->opaque];
            [encoder setDepthStencilState:device->depthWrite];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:6
                      instanceCount:opaqueCount
                       baseInstance:0];
        }
        [encoder setDepthStencilState:device->depthTest];
        for (const render::DrawRun &run : batches.runs()) {
            id<MTLRenderPipelineState> pipeline =
                device->pipelines[static_cast<std::size_t>(run.pipeline)];
            if (counting && run.pipeline == render::UberPipeline::sourceOver) {
                pipeline = device->counting;
            }
            if (pipeline == nil) {
                continue;
            }
            [encoder setRenderPipelineState:pipeline];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:6
                      instanceCount:run.count
                       baseInstance:opaqueCount + run.first];
        }
    }
};

MetalSurface::MetalSurface(std::shared_ptr<MetalDeviceState> device, hal::SurfaceDesc desc)
    : state_(std::make_unique<MetalSurfaceState>()) {
    state_->device = std::move(device);
    state_->extent = desc.extent;
    @autoreleasepool {
        state_->createTargets();
    }
}

MetalSurface::~MetalSurface() = default;

hal::Extent MetalSurface::extent() const noexcept { return state_->extent; }

void MetalSurface::resize(hal::Extent extent) {
    state_->extent = extent;
    @autoreleasepool {
        state_->createTargets();
    }
}

void MetalSurface::loseDevice() noexcept {
    state_->lost = true;
    state_->releaseTargets();
}

bool MetalSurface::rebuild() {
    @autoreleasepool {
        state_->createTargets();
    }
    state_->lost = state_->queue == nil || state_->color == nil;
    return !state_->lost;
}

bool MetalSurface::lost() const noexcept { return state_->lost; }

void MetalSurface::setEarlyDepth(bool enabled) noexcept { state_->earlyDepth = enabled; }
void MetalSurface::setFragmentCounting(bool enabled) noexcept { state_->counting = enabled; }
std::uint32_t MetalSurface::lastFragmentCount() const noexcept { return state_->fragments; }

bool MetalSurface::render(const render::FrameRequest &request) {
    if (state_->lost || request.damage == nullptr) {
        return false;
    }
    @autoreleasepool {
        state_->uploadFrame(request.view);
        *static_cast<std::uint32_t *>(state_->counter.contents) = 0;
        FrameUniforms uniforms{
            {static_cast<float>(state_->extent.width), static_cast<float>(state_->extent.height)},
            static_cast<std::uint32_t>(request.view.primitives.size()),
            static_cast<std::uint32_t>(request.view.textures.size()),
            state_->counting ? 1u : 0u,
            0,
            {0, 0}};
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = state_->color;
        pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.depthAttachment.texture = state_->depth;
        pass.depthAttachment.loadAction = MTLLoadActionLoad;
        pass.depthAttachment.storeAction = MTLStoreActionStore;
        id<MTLCommandBuffer> commands = [state_->queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
        const hal::Rect viewport{0, 0, static_cast<std::int32_t>(state_->extent.width),
                                 static_cast<std::int32_t>(state_->extent.height)};
        request.damage->forEachRect([&](hal::Rect rect) {
            const hal::Rect clipped{std::max(rect.x0, viewport.x0), std::max(rect.y0, viewport.y0),
                                    std::min(rect.x1, viewport.x1), std::min(rect.y1, viewport.y1)};
            if (!clipped.empty()) {
                state_->encodeRect(encoder, clipped, request, uniforms);
            }
        });
        [encoder endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        state_->fragments = *static_cast<const std::uint32_t *>(state_->counter.contents);
        if (commands.status != MTLCommandBufferStatusCompleted) {
            state_->lost = true;
            return false;
        }
    }
    return true;
}

bool MetalSurface::readback(std::span<float> rgba) {
    const std::size_t pixels =
        static_cast<std::size_t>(state_->extent.width) * state_->extent.height;
    if (state_->lost || rgba.size() != pixels * 4) {
        return false;
    }
    @autoreleasepool {
        const NSUInteger rowBytes = state_->extent.width * 8;
        upload(state_->device->device, state_->readback, nullptr, rowBytes * state_->extent.height);
        id<MTLCommandBuffer> commands = [state_->queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit copyFromTexture:state_->color
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(state_->extent.width, state_->extent.height, 1)
                            toBuffer:state_->readback
                   destinationOffset:0
              destinationBytesPerRow:rowBytes
            destinationBytesPerImage:rowBytes * state_->extent.height];
        [blit endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        const auto *halves = static_cast<const std::uint16_t *>(state_->readback.contents);
        for (std::size_t i = 0; i < pixels * 4; ++i) {
            rgba[i] = halfToFloat(halves[i]);
        }
    }
    return true;
}

bool MetalSurface::readbackPresented(std::span<std::uint8_t> rgba, std::uint32_t frame) {
    const std::size_t pixels =
        static_cast<std::size_t>(state_->extent.width) * state_->extent.height;
    if (state_->lost || rgba.size() != pixels * 4) {
        return false;
    }
    @autoreleasepool {
        upload(state_->device->device, state_->readback, nullptr, pixels * 4);
        id<MTLCommandBuffer> commands = [state_->queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
        [encoder setComputePipelineState:state_->device->present];
        [encoder setTexture:state_->color atIndex:0];
        [encoder setBuffer:state_->readback offset:0 atIndex:0];
        [encoder setBytes:&frame length:sizeof(frame) atIndex:1];
        [encoder dispatchThreads:MTLSizeMake(state_->extent.width, state_->extent.height, 1)
            threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        [encoder endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        std::memcpy(rgba.data(), state_->readback.contents, pixels * 4);
    }
    return true;
}
}
