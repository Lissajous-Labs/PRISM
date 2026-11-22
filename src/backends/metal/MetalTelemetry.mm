#include "backends/metal/MetalTelemetry.h"

#include "backends/metal/MetalState.h"

#include <algorithm>
#include <new>
#include <stdexcept>

namespace prism::backends {
namespace {
struct TelemetryScopeUniforms {
    std::uint32_t end, window, columns, mask;
    std::uint32_t channel;
    float gain;
    std::uint32_t reserved0, reserved1;
};
static_assert(sizeof(TelemetryScopeUniforms) == 32);
}

struct MetalTelemetry::Impl {
    std::shared_ptr<MetalDeviceState> device;
    id<MTLBuffer> ring;
    id<MTLComputePipelineState> scopePipeline;
    id<MTLTexture> strip;
    telemetry::UmaLayout *layout = nullptr;
    telemetry::UmaSnapshot snapshot;
    TelemetryScopeUniforms uniforms{};
    std::uint32_t columns = 0;
    bool pending = false;
    bool ready = false;
};

MetalTelemetry::MetalTelemetry(std::shared_ptr<MetalDeviceState> device, std::uint32_t scopeColumns)
    : impl_(std::make_unique<Impl>()) {
    if (scopeColumns == 0 || scopeColumns > 8192) {
        throw std::invalid_argument("Invalid telemetry scope width");
    }
    @autoreleasepool {
        auto &s = *impl_;
        s.device = std::move(device);
        s.columns = scopeColumns;
        auto gpu = s.device->device;
        s.ring = [gpu newBufferWithLength:sizeof(telemetry::UmaLayout)
                                  options:MTLResourceStorageModeShared];
        s.scopePipeline =
            [gpu newComputePipelineStateWithFunction:[s.device->library
                                                         newFunctionWithName:@"telemetryScope"]
                                               error:nil];
        auto *desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG32Float
                                                               width:scopeColumns
                                                              height:1
                                                           mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        desc.storageMode = MTLStorageModePrivate;
        s.strip = [gpu newTextureWithDescriptor:desc];
        if (!s.ring || !s.scopePipeline || !s.strip) {
            throw std::runtime_error("Cannot create Metal telemetry resources");
        }
        s.layout = new (s.ring.contents) telemetry::UmaLayout();
        s.device->telemetries.push_back(this);
    }
}

MetalTelemetry::~MetalTelemetry() {
    std::erase(impl_->device->telemetries, this);
    impl_->layout->~UmaLayout();
}

telemetry::UmaLayout &MetalTelemetry::layout() noexcept { return *impl_->layout; }

const telemetry::UmaSnapshot &MetalTelemetry::lastSnapshot() const noexcept {
    return impl_->snapshot;
}

bool MetalTelemetry::prepareScope(const ScopeRequest &request) noexcept {
    auto &s = *impl_;
    if (request.window == 0 || request.window > telemetry::umaCapacity / 2 || request.channel > 2 ||
        !telemetry::snapshot(*s.layout, s.snapshot) || s.snapshot.frames < request.window) {
        return false;
    }
    s.uniforms = {static_cast<std::uint32_t>(s.snapshot.frames & telemetry::umaMask),
                  request.window,
                  s.columns,
                  telemetry::umaMask,
                  request.channel,
                  request.gain,
                  0,
                  0};
    s.pending = true;
    return true;
}

bool MetalTelemetry::encode(void *commandBuffer) {
    @autoreleasepool {
        auto &s = *impl_;
        if (!s.pending || commandBuffer == nullptr) {
            return false;
        }
        s.pending = false;
        auto commands = (__bridge id<MTLCommandBuffer>)commandBuffer;
        auto encoder = [commands computeCommandEncoder];
        [encoder setComputePipelineState:s.scopePipeline];
        [encoder setBuffer:s.ring offset:telemetry::umaScopeOffset atIndex:0];
        [encoder setBuffer:s.ring
                    offset:telemetry::umaScopeOffset + telemetry::umaCapacity * sizeof(float)
                   atIndex:1];
        [encoder setBytes:&s.uniforms length:sizeof(s.uniforms) atIndex:2];
        [encoder setTexture:s.strip atIndex:0];
        const NSUInteger width = s.scopePipeline.threadExecutionWidth;
        [encoder dispatchThreads:MTLSizeMake(s.columns, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(std::min<NSUInteger>(width, s.columns), 1, 1)];
        [encoder endEncoding];
        s.ready = true;
        return true;
    }
}

bool MetalTelemetry::dispatch() {
    @autoreleasepool {
        auto &s = *impl_;
        auto commands = [s.device->queue commandBuffer];
        if (!encode((__bridge void *)commands)) {
            return false;
        }
        [commands commit];
        [commands waitUntilCompleted];
        return commands.status == MTLCommandBufferStatusCompleted;
    }
}

render::TextureView MetalTelemetry::scope() const noexcept {
    const auto &s = *impl_;
    return s.ready || s.pending ? render::TextureView{s.columns,
                                                      1,
                                                      2,
                                                      {},
                                                      hal::PixelFormat::rg32Float,
                                                      0,
                                                      (__bridge void *)s.strip}
                                : render::TextureView{};
}
}
