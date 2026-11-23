#include "backends/metal/MetalTelemetry.h"

#include "backends/metal/MetalState.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <new>
#include <stdexcept>
#include <vector>

namespace prism::backends {
namespace {
struct TelemetryScopeUniforms {
    std::uint32_t end, window, columns, mask;
    std::uint32_t channel;
    float gain;
    std::uint32_t reserved0, reserved1;
};
static_assert(sizeof(TelemetryScopeUniforms) == 32);

struct SpectrumBandUniforms {
    std::uint32_t end, size, ringOffset, densityOffset;
    float scale, binWidth;
    std::uint32_t mask, log2Half;
};
static_assert(sizeof(SpectrumBandUniforms) == 32);

struct SpectrumTerm {
    float weight, low, high;
    std::uint32_t band;
};
static_assert(sizeof(SpectrumTerm) == 16);

constexpr std::size_t spectrumSlots = 3;
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

    id<MTLComputePipelineState> binsPipeline;
    id<MTLComputePipelineState> knotsPipeline;
    id<MTLBuffer> density;
    id<MTLBuffer> terms;
    std::array<id<MTLBuffer>, spectrumSlots> power{};
    std::array<SpectrumBandUniforms, telemetry::spectrumBands.size()> bands{};
    std::uint32_t knots = 0;
    bool spectrumPending = false;
    std::uint64_t spectrumEncoded = 0;
    std::shared_ptr<std::atomic<std::uint64_t>> spectrumCompleted =
        std::make_shared<std::atomic<std::uint64_t>>(0);
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

bool MetalTelemetry::enableSpectrum(const telemetry::SpectrumSettings &settings) {
    @autoreleasepool {
        auto &s = *impl_;
        auto gpu = s.device->device;
        const auto pipeline = [&](NSString *name) {
            return [gpu
                newComputePipelineStateWithFunction:[s.device->library newFunctionWithName:name]
                                              error:nil];
        };
        s.binsPipeline = pipeline(@"umaSpectrumBins");
        s.knotsPipeline = pipeline(@"umaSpectrumKnots");

        std::uint32_t densityFloats = 0;
        for (std::size_t b = 0; b < telemetry::spectrumBands.size(); ++b) {
            const auto &spec = telemetry::spectrumBands[b];
            const double rate = settings.sampleRate / spec.decimation;
            s.bands[b] = {
                0,
                spec.size,
                static_cast<std::uint32_t>(b * telemetry::umaCapacity),
                densityFloats,
                static_cast<float>(1.0 / (rate * telemetry::blackmanHarrisPower(spec.size))),
                static_cast<float>(rate / spec.size),
                telemetry::umaMask,
                static_cast<std::uint32_t>(std::countr_zero(spec.size / 2))};
            densityFloats += spec.size / 2 + 1;
        }

        const auto edges = telemetry::spectrumEdges(settings);
        s.knots = static_cast<std::uint32_t>(edges.size() - 1);
        std::vector<SpectrumTerm> terms(static_cast<std::size_t>(s.knots) * 4);
        const double ratio = std::log(settings.highHz / settings.lowHz) / (s.knots - 1);
        for (std::uint32_t i = 0; i < s.knots; ++i) {
            const double hz = settings.lowHz * std::exp(ratio * i);
            for (std::uint32_t b = 0; b < 4; ++b) {
                terms[i * 4 + b] = {static_cast<float>(telemetry::crossoverWeight(b, hz)),
                                    static_cast<float>(edges[i]), static_cast<float>(edges[i + 1]),
                                    b};
            }
        }

        s.density = [gpu newBufferWithLength:densityFloats * sizeof(float)
                                     options:MTLResourceStorageModeShared];
        s.terms = [gpu newBufferWithBytes:terms.data()
                                   length:terms.size() * sizeof(SpectrumTerm)
                                  options:MTLResourceStorageModeShared];
        for (auto &buffer : s.power) {
            buffer = [gpu newBufferWithLength:s.knots * sizeof(float)
                                      options:MTLResourceStorageModeShared];
        }
        return s.binsPipeline && s.knotsPipeline && s.density && s.terms && s.power[0];
    }
}

bool MetalTelemetry::prepareSpectrum() noexcept {
    auto &s = *impl_;
    if (s.knots == 0 || !telemetry::snapshot(*s.layout, s.snapshot)) {
        return false;
    }
    for (std::size_t b = 0; b < s.bands.size(); ++b) {
        if (s.snapshot.bandFrames[b] < s.bands[b].size) {
            return false;
        }
        s.bands[b].end = static_cast<std::uint32_t>(s.snapshot.bandFrames[b] & telemetry::umaMask);
    }
    s.spectrumPending = true;
    return true;
}

std::span<const float> MetalTelemetry::spectrumPower() const noexcept {
    const auto &s = *impl_;
    const std::uint64_t completed = s.spectrumCompleted->load(std::memory_order_acquire);
    if (completed == 0) {
        return {};
    }
    const auto *data =
        static_cast<const float *>(s.power[(completed - 1) % spectrumSlots].contents);
    return {data, s.knots};
}

std::span<const float> MetalTelemetry::spectrumDensity(std::size_t band) const noexcept {
    const auto &s = *impl_;
    if (s.density == nil || band >= s.bands.size()) {
        return {};
    }
    const auto *data = static_cast<const float *>(s.density.contents);
    return {data + s.bands[band].densityOffset, s.bands[band].size / 2 + 1};
}

std::uint64_t MetalTelemetry::spectrumGeneration() const noexcept {
    return impl_->spectrumCompleted->load(std::memory_order_acquire);
}

bool MetalTelemetry::encode(void *commandBuffer) {
    @autoreleasepool {
        auto &s = *impl_;
        if ((!s.pending && !s.spectrumPending) || commandBuffer == nullptr) {
            return false;
        }
        auto commands = (__bridge id<MTLCommandBuffer>)commandBuffer;
        auto encoder = [commands computeCommandEncoder];
        if (s.pending) {
            s.pending = false;
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
            s.ready = true;
        }
        if (s.spectrumPending) {
            s.spectrumPending = false;
            const std::uint64_t generation = ++s.spectrumEncoded;
            id<MTLBuffer> power = s.power[(generation - 1) % spectrumSlots];
            [encoder setComputePipelineState:s.binsPipeline];
            [encoder setBuffer:s.ring offset:telemetry::umaBandOffset atIndex:0];
            [encoder setBytes:s.bands.data() length:sizeof(s.bands) atIndex:1];
            [encoder setBuffer:s.density offset:0 atIndex:2];
            const NSUInteger threads =
                std::min<NSUInteger>(512, s.binsPipeline.maxTotalThreadsPerThreadgroup);
            [encoder dispatchThreadgroups:MTLSizeMake(s.bands.size(), 1, 1)
                    threadsPerThreadgroup:MTLSizeMake(threads, 1, 1)];
            [encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
            [encoder setComputePipelineState:s.knotsPipeline];
            [encoder setBuffer:s.density offset:0 atIndex:0];
            [encoder setBytes:s.bands.data() length:sizeof(s.bands) atIndex:1];
            [encoder setBuffer:s.terms offset:0 atIndex:2];
            [encoder setBuffer:power offset:0 atIndex:3];
            [encoder setBytes:&s.knots length:sizeof(s.knots) atIndex:4];
            const NSUInteger width = s.knotsPipeline.threadExecutionWidth;
            [encoder dispatchThreads:MTLSizeMake(s.knots, 1, 1)
                threadsPerThreadgroup:MTLSizeMake(std::min<NSUInteger>(width, s.knots), 1, 1)];
            const auto completed = s.spectrumCompleted;
            [commands addCompletedHandler:^(id<MTLCommandBuffer> done) {
              if (done.status == MTLCommandBufferStatusCompleted) {
                  completed->store(generation, std::memory_order_release);
              }
            }];
        }
        [encoder endEncoding];
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
        if (commands.status != MTLCommandBufferStatusCompleted) {
            return false;
        }
        if (s.spectrumEncoded > s.spectrumCompleted->load(std::memory_order_acquire)) {
            s.spectrumCompleted->store(s.spectrumEncoded, std::memory_order_release);
        }
        return true;
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
