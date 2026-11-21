#include "backends/metal/MetalBackend.h"
#include "backends/metal/MetalSurfaceState.h"
#include "backends/metal/MetalUpload.h"

#include <cstring>

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
double MetalSurface::lastGpuMilliseconds() const noexcept { return state_->gpuMilliseconds; }

bool MetalSurface::render(const render::FrameRequest &request) {
    if (state_->lost || request.damage == nullptr) {
        return false;
    }
    @autoreleasepool {
        state_->uploadFrame(request.view);
        *static_cast<std::uint32_t *>(state_->counter.contents) = 0;
        id<MTLCommandBuffer> commands = [state_->queue commandBuffer];
        state_->encodeFrame(commands, request);
        [commands commit];
        [commands waitUntilCompleted];
        state_->fragments = *static_cast<const std::uint32_t *>(state_->counter.contents);
        state_->gpuMilliseconds = (commands.GPUEndTime - commands.GPUStartTime) * 1000.0;
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
        std::memcpy(rgba.data(), state_->readback.contents, pixels * 4);
    }
    return true;
}

bool MetalSurface::presentToLayer(void *layer, std::uint32_t frame) {
    if (state_->lost || layer == nullptr) {
        return false;
    }
    @autoreleasepool {
        CAMetalLayer *target = (__bridge CAMetalLayer *)layer;
        target.device = state_->device->device;
        target.pixelFormat = MTLPixelFormatBGRA8Unorm;
        target.framebufferOnly = NO;
        target.drawableSize = CGSizeMake(state_->extent.width, state_->extent.height);
        id<CAMetalDrawable> drawable = [target nextDrawable];
        if (drawable == nil) {
            return false;
        }
        id<MTLCommandBuffer> commands = [state_->queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
        [encoder setComputePipelineState:state_->device->presentDrawable];
        [encoder setTexture:state_->presentedTexture() atIndex:0];
        [encoder setTexture:drawable.texture atIndex:1];
        [encoder setBytes:&frame length:sizeof(frame) atIndex:0];
        [encoder dispatchThreads:MTLSizeMake(drawable.texture.width, drawable.texture.height, 1)
            threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        [encoder endEncoding];
        if (target.presentsWithTransaction) {
            [commands commit];
            [commands waitUntilScheduled];
            [drawable present];
        } else {
            [commands presentDrawable:drawable];
            [commands commit];
        }
        return commands.error == nil;
    }
}

void MetalSurface::waitIdle() {
    @autoreleasepool {
        id<MTLCommandBuffer> fence = [state_->queue commandBuffer];
        [fence commit];
        [fence waitUntilCompleted];
    }
}
}
