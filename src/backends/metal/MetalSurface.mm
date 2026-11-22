#include "backends/metal/MetalBackend.h"
#include "backends/metal/MetalSurfaceState.h"
#include "backends/metal/MetalUpload.h"

#include <cstring>
#include <utility>

#import <QuartzCore/CAMetalLayer.h>

namespace prism::backends {
MetalSurface::MetalSurface(std::shared_ptr<MetalDeviceState> device, hal::SurfaceDesc desc)
    : state_(std::make_unique<MetalSurfaceState>()) {
    state_->device = std::move(device);
    state_->extent = desc.extent;
    @autoreleasepool {
        state_->createTargets();
    }
}

MetalSurface::~MetalSurface() { waitIdle(); }

hal::Extent MetalSurface::extent() const noexcept { return state_->extent; }

void MetalSurface::resize(hal::Extent extent) {
    waitIdle();
    state_->recoverDamage = true;
    state_->extent = extent;
    @autoreleasepool {
        state_->createTargets();
    }
}

void MetalSurface::loseDevice() noexcept {
    waitIdle();
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
std::uint32_t MetalSurface::lastFragmentCount() const noexcept {
    state_->pollFrames();
    return state_->fragments;
}
double MetalSurface::lastGpuMilliseconds() const noexcept {
    state_->pollFrames();
    return state_->gpuMilliseconds;
}
std::uint32_t MetalSurface::lastDrawCalls() const noexcept { return state_->drawCalls; }
std::size_t MetalSurface::lastUploadBytes() const noexcept { return state_->uploadBytes; }
bool MetalSurface::lastSubmitted() const noexcept { return state_->submitted; }

namespace {
id<CAMetalDrawable> encodePresentation(MetalSurfaceState &state, id<MTLCommandBuffer> commands,
                                       CAMetalLayer *target) {
    if (target.device != state.device->device) {
        target.device = state.device->device;
    }
    const CGSize size = CGSizeMake(state.extent.width, state.extent.height);
    if (!CGSizeEqualToSize(target.drawableSize, size)) {
        target.drawableSize = size;
    }
    id<CAMetalDrawable> drawable = [target nextDrawable];
    if (drawable == nil) {
        return nil;
    }

    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
    [encoder setRenderPipelineState:state.device->presentDrawable];
    [encoder setFragmentTexture:state.presentedTexture() atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder endEncoding];
    ++state.drawCalls;
    return drawable;
}
}

bool MetalSurface::render(const render::FrameRequest &request) {
    bool presented = false;
    return submit(request, nullptr, 0, presented);
}

bool MetalSurface::renderToLayer(const render::FrameRequest &request, void *layer,
                                 double displayPeriod, bool &presented) {
    if (layer == nullptr) {
        presented = false;
        return false;
    }

    return submit(request, layer, displayPeriod, presented);
}

bool MetalSurface::submit(const render::FrameRequest &request, void *layer, double displayPeriod,
                          bool &presented) {
    presented = false;
    state_->pollFrames();
    if (state_->lost || request.damage == nullptr || state_->completed == nil) {
        return false;
    }
    state_->submitted =
        !request.damage->empty() || request.bloom != nullptr || state_->recoverDamage;
    if (!state_->submitted) {
        state_->gpuMilliseconds = 0;
        state_->drawCalls = 0;
        state_->uploadBytes = 0;
        return true;
    }
    @autoreleasepool {
        auto &slot = state_->frames[state_->frameNumber % state_->frames.size()];
        if (slot.fenceValue > state_->completed.signaledValue) {
            state_->submitted = false;
            state_->recoverDamage = true;
            return true;
        }

        std::swap(static_cast<MetalFrameResources &>(*state_), slot);
        state_->uploadFrame(request.view);
        *static_cast<std::uint32_t *>(state_->counter.contents) = 0;
        id<MTLCommandBuffer> commands = [state_->queue commandBuffer];
        state_->encodeFrame(commands, request);
        CAMetalLayer *target = (__bridge CAMetalLayer *)layer;
        id<CAMetalDrawable> drawable =
            target != nil ? encodePresentation(*state_, commands, target) : nil;
        state_->commands = commands;
        state_->fenceValue = ++state_->frameNumber;
        [commands encodeSignalEvent:state_->completed value:state_->fenceValue];
        if (drawable != nil && !target.presentsWithTransaction) {
            [commands presentDrawable:drawable
                 afterMinimumDuration:std::max(0.0, displayPeriod - 0.0005)];
        }
        std::swap(static_cast<MetalFrameResources &>(*state_), slot);
        [commands commit];
        if (drawable != nil && target.presentsWithTransaction) {
            [commands waitUntilScheduled];
            [drawable present];
        }
        presented = drawable != nil;
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
        [blit copyFromTexture:state_->presentedTexture()
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
        state_->pollFrames();
        if (commands.status != MTLCommandBufferStatusCompleted || state_->lost) {
            return false;
        }
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
        [encoder setTexture:state_->presentedTexture() atIndex:0];
        [encoder setBuffer:state_->readback offset:0 atIndex:0];
        [encoder setBytes:&frame length:sizeof(frame) atIndex:1];
        [encoder dispatchThreads:MTLSizeMake(state_->extent.width, state_->extent.height, 1)
            threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        [encoder endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        state_->pollFrames();
        if (commands.status != MTLCommandBufferStatusCompleted || state_->lost) {
            return false;
        }
        std::memcpy(rgba.data(), state_->readback.contents, pixels * 4);
    }
    return true;
}

void MetalSurface::waitIdle() {
    @autoreleasepool {
        id<MTLCommandBuffer> fence = [state_->queue commandBuffer];
        [fence commit];
        [fence waitUntilCompleted];
        state_->pollFrames();
    }
}
}
