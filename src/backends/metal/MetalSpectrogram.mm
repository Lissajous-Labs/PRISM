#include "backends/metal/MetalSpectrogram.h"

#include "backends/metal/MetalState.h"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace prism::backends {
namespace {
struct SpectrogramUniforms {
    std::uint32_t frames, period, rows, first;
    float tilt, mirror, bend, sync;
    float phaseOffset, quantization, sharpness, amAmount;
    float amHarmonic, amPhase;
    std::uint32_t bits, reserved;
};
static_assert(sizeof(SpectrogramUniforms) == 64);
id<MTLTexture> texture(id<MTLDevice> device, MTLPixelFormat format, std::size_t width,
                       std::size_t height, bool writable) {
    auto *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                                    width:width
                                                                   height:height
                                                                mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead | (writable ? MTLTextureUsageShaderWrite : 0);
    desc.storageMode = writable ? MTLStorageModePrivate : MTLStorageModeShared;
    return [device newTextureWithDescriptor:desc];
}

void scan(id<MTLComputeCommandEncoder> encoder, id<MTLComputePipelineState> pipeline,
          std::uint32_t groups, std::uint32_t items) {
    const NSUInteger width = pipeline.threadExecutionWidth;
    const NSUInteger threads = std::max(
        width, std::min<NSUInteger>(pipeline.maxTotalThreadsPerThreadgroup, std::bit_ceil(items)));
    const NSUInteger scratchBytes = ((threads / width + 1) * sizeof(float) + 15) & ~NSUInteger{15};
    [encoder setComputePipelineState:pipeline];
    [encoder setThreadgroupMemoryLength:scratchBytes atIndex:0];
    [encoder dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
}
}
struct MetalSpectrogram::Impl {
    std::shared_ptr<MetalDeviceState> device;
    id<MTLComputePipelineState> fft, prefixRows, prefixColumns;
    id<MTLTexture> source, field, integral;
    std::uint32_t frames, period, rows;
    bool ready = false;
    double milliseconds = 0;
    SpectrogramUniforms uniforms{};
    std::uint32_t pendingFrames = 0;
};
MetalSpectrogram::MetalSpectrogram(std::shared_ptr<MetalDeviceState> device,
                                   math::WavetableView table, std::uint32_t rows)
    : impl_(std::make_unique<Impl>()) {
    if (table.frames == 0 || table.frames > UINT32_MAX || table.period < 8 || table.period > 2048 ||
        !std::has_single_bit(table.period) || rows == 0 || rows > 256 ||
        table.samples.size() < table.frames * table.period) {
        throw std::invalid_argument("Invalid Metal spectrogram dimensions");
    }
    @autoreleasepool {
        auto &s = *impl_;
        s.device = std::move(device);
        s.frames = static_cast<std::uint32_t>(table.frames);
        s.period = static_cast<std::uint32_t>(table.period);
        s.rows = rows;
        auto gpu = s.device->device;
        if (not s.device->queue) {
            throw std::runtime_error("Cannot access shared Metal command queue");
        }
        const auto pipeline = [&](NSString *name) {
            return [gpu
                newComputePipelineStateWithFunction:[s.device->library newFunctionWithName:name]
                                              error:nil];
        };
        s.fft = pipeline(@"spectrogramFft");
        s.prefixRows = pipeline(@"spectrogramPrefixRows");
        s.prefixColumns = pipeline(@"spectrogramPrefixColumns");
        s.source = texture(gpu, MTLPixelFormatR32Float, s.period, s.frames, false);
        s.field = texture(gpu, MTLPixelFormatR16Float, s.frames, s.rows, true);
        s.integral = texture(gpu, MTLPixelFormatR32Float, s.frames + 1, s.rows + 1, true);
        if (!s.fft || !s.prefixRows || !s.prefixColumns || !s.source || !s.field || !s.integral) {
            throw std::runtime_error("Cannot create Metal spectral compute resources");
        }
        [s.source replaceRegion:MTLRegionMake2D(0, 0, s.period, s.frames)
                    mipmapLevel:0
                      withBytes:table.samples.data()
                    bytesPerRow:s.period * sizeof(float)];
        s.device->spectrograms.push_back(this);
    }
}
MetalSpectrogram::~MetalSpectrogram() { std::erase(impl_->device->spectrograms, this); }
bool MetalSpectrogram::prepare(const math::WavetableWarp &warp, float tilt,
                               std::uint32_t firstFrame, std::uint32_t count) noexcept {
    auto &s = *impl_;
    const bool pending = s.pendingFrames != 0;
    s.pendingFrames = 0;
    if (firstFrame >= s.frames || count == 0) {
        return false;
    }
    if (!s.ready || pending) {
        firstFrame = 0;
        count = s.frames;
    }
    count = std::min(count, s.frames - firstFrame);
    s.uniforms = SpectrogramUniforms{s.frames,
                                     s.period,
                                     s.rows,
                                     firstFrame,
                                     tilt,
                                     static_cast<float>(warp.mirror),
                                     static_cast<float>(warp.bend),
                                     static_cast<float>(warp.sync),
                                     static_cast<float>(warp.phaseOffset),
                                     static_cast<float>(warp.quantization),
                                     static_cast<float>(warp.sharpness),
                                     static_cast<float>(warp.amplitudeModulation),
                                     static_cast<float>(warp.modulationHarmonic),
                                     static_cast<float>(warp.modulationPhase),
                                     static_cast<std::uint32_t>(std::countr_zero(s.period)),
                                     0};
    s.pendingFrames = count;
    return true;
}
bool MetalSpectrogram::dispatch() {
    @autoreleasepool {
        auto &s = *impl_;
        auto commands = [s.device->queue commandBuffer];
        if (!encode((__bridge void *)commands)) {
            return false;
        }
        [commands commit];
        [commands waitUntilCompleted];
        s.milliseconds = (commands.GPUEndTime - commands.GPUStartTime) * 1000;
        s.ready = commands.status == MTLCommandBufferStatusCompleted;
        return s.ready;
    }
}

bool MetalSpectrogram::encode(void *commandBuffer) {
    @autoreleasepool {
        auto &s = *impl_;
        if (s.pendingFrames == 0 || commandBuffer == nullptr) {
            return false;
        }
        const auto &u = s.uniforms;
        const auto count = s.pendingFrames;
        s.pendingFrames = 0;
        auto commands = (__bridge id<MTLCommandBuffer>)commandBuffer;
        auto encoder = [commands computeCommandEncoder];
        [encoder setComputePipelineState:s.fft];
        [encoder setTexture:s.source atIndex:0];
        [encoder setTexture:s.field atIndex:1];
        [encoder setBytes:&u length:sizeof(u) atIndex:0];
        const auto threads = std::min<NSUInteger>(s.fft.maxTotalThreadsPerThreadgroup,
                                                  std::max<std::uint32_t>(s.period / 2, 32));
        [encoder dispatchThreadgroups:MTLSizeMake(count, 1, 1)
                threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
        [encoder memoryBarrierWithScope:MTLBarrierScopeTextures];
        [encoder setTexture:s.field atIndex:0];
        [encoder setTexture:s.integral atIndex:1];
        scan(encoder, s.prefixRows, s.rows, s.frames);
        [encoder memoryBarrierWithScope:MTLBarrierScopeTextures];
        [encoder setTexture:s.integral atIndex:0];
        scan(encoder, s.prefixColumns, s.frames + 1, s.rows);
        [encoder endEncoding];
        s.ready = true;
        return true;
    }
}
render::TextureView MetalSpectrogram::field() const noexcept {
    const auto &s = *impl_;
    return s.ready || s.pendingFrames != 0 ? render::TextureView{s.frames,
                                                                 s.rows,
                                                                 1,
                                                                 {},
                                                                 hal::PixelFormat::r16Float,
                                                                 0,
                                                                 (__bridge void *)s.field}
                                           : render::TextureView{};
}
render::TextureView MetalSpectrogram::integral() const noexcept {
    const auto &s = *impl_;
    return s.ready || s.pendingFrames != 0 ? render::TextureView{s.frames + 1,
                                                                 s.rows + 1,
                                                                 1,
                                                                 {},
                                                                 hal::PixelFormat::rg32Float,
                                                                 0,
                                                                 (__bridge void *)s.integral}
                                           : render::TextureView{};
}
double MetalSpectrogram::lastGpuMilliseconds() const noexcept { return impl_->milliseconds; }
}
