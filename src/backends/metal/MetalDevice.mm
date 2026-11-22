#include "backends/metal/MetalBackend.h"
#include "backends/metal/MetalState.h"
#include "backends/metal/MetalWavetable3d.h"
#include "render/ShaderLibrary.h"

namespace prism::backends {
namespace {
constexpr MTLPixelFormat colorFormat = MTLPixelFormatRGBA16Float;
constexpr MTLPixelFormat depthFormat = MTLPixelFormatDepth32Float;

id<MTLFunction> function(id<MTLLibrary> library, const char *name) {
    return [library newFunctionWithName:[NSString stringWithUTF8String:name]];
}

void configureBlend(MTLRenderPipelineColorAttachmentDescriptor *attachment,
                    render::UberPipeline pipeline) {
    attachment.pixelFormat = colorFormat;
    attachment.blendingEnabled = YES;
    attachment.rgbBlendOperation = MTLBlendOperationAdd;
    attachment.alphaBlendOperation = MTLBlendOperationAdd;
    attachment.sourceRGBBlendFactor = MTLBlendFactorOne;
    attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
    attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    switch (pipeline) {
    case render::UberPipeline::sourceOver:
    case render::UberPipeline::analytic:
    case render::UberPipeline::textured:
        attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        break;
    case render::UberPipeline::screen:
        attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceColor;
        break;
    case render::UberPipeline::additive:
        attachment.destinationRGBBlendFactor = MTLBlendFactorOne;
        attachment.destinationAlphaBlendFactor = MTLBlendFactorOne;
        break;
    case render::UberPipeline::subpixel:
        attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSource1Color;
        break;
    case render::UberPipeline::replace:
    case render::UberPipeline::multiply:
        attachment.blendingEnabled = NO;
        break;
    }
}

id<MTLRenderPipelineState> buildPipeline(id<MTLDevice> device, id<MTLLibrary> library,
                                         const char *vertexName, const char *fragmentName,
                                         render::UberPipeline blend) {
    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = function(library, vertexName);
    MTLFunctionConstantValues *constants = [MTLFunctionConstantValues new];
    const std::uint32_t shadingClass = blend == render::UberPipeline::analytic   ? 1
                                       : blend == render::UberPipeline::textured ? 2
                                                                                 : 0;
    [constants setConstantValue:&shadingClass type:MTLDataTypeUInt atIndex:0];
    descriptor.fragmentFunction =
        [library newFunctionWithName:[NSString stringWithUTF8String:fragmentName]
                      constantValues:constants
                               error:nil];
    descriptor.depthAttachmentPixelFormat = depthFormat;
    configureBlend(descriptor.colorAttachments[0], blend);
    NSError *error = nil;
    id<MTLRenderPipelineState> state = [device newRenderPipelineStateWithDescriptor:descriptor
                                                                              error:&error];
    return error ? nil : state;
}

id<MTLDepthStencilState> depthState(id<MTLDevice> device, MTLCompareFunction compare, BOOL write) {
    MTLDepthStencilDescriptor *descriptor = [MTLDepthStencilDescriptor new];
    descriptor.depthCompareFunction = compare;
    descriptor.depthWriteEnabled = write;
    return [device newDepthStencilStateWithDescriptor:descriptor];
}

const char *fragmentFor(render::UberPipeline pipeline) {
    switch (pipeline) {
    case render::UberPipeline::multiply:
        return "uberMultiplyFragment";
    case render::UberPipeline::subpixel:
        return "uberLcdFragment";
    default:
        return "uberFragment";
    }
}
}

std::shared_ptr<MetalDeviceState> createMetalDeviceState() {
    auto state = std::make_shared<MetalDeviceState>();
    state->device = MTLCreateSystemDefaultDevice();
    if (state->device == nil) {
        return nullptr;
    }
    state->queue = [state->device newCommandQueue];
    if (state->queue == nil) {
        return nullptr;
    }
    const auto source = render::metalShaderSource();
    NSString *text = [[NSString alloc] initWithBytes:source.data()
                                              length:source.size()
                                            encoding:NSUTF8StringEncoding];
    MTLCompileOptions *options = [MTLCompileOptions new];
#if defined(__MAC_15_0)
    if (@available(macOS 15.0, *)) {
        options.mathMode = MTLMathModeSafe;
    } else {
        options.fastMathEnabled = NO;
    }
#else
    options.fastMathEnabled = NO;
#endif
    NSError *error = nil;
    state->library = [state->device newLibraryWithSource:text options:options error:&error];
    if (state->library == nil) {
        NSLog(@"Metal shader compilation failed: %@", error);
        return nullptr;
    }
    state->programmableBlending = [state->device supportsFamily:MTLGPUFamilyApple1];
    for (std::size_t i = 0; i < uberPipelineCount; ++i) {
        const auto pipeline = static_cast<render::UberPipeline>(i);
        if (pipeline == render::UberPipeline::multiply && !state->programmableBlending) {
            continue;
        }
        state->pipelines[i] = buildPipeline(state->device, state->library, "uberVertex",
                                            fragmentFor(pipeline), pipeline);
        if (state->pipelines[i] == nil) {
            return nullptr;
        }
    }
    state->opaque = buildPipeline(state->device, state->library, "uberCoreVertex",
                                  "uberCoreFragment", render::UberPipeline::replace);
    state->countingCore = buildPipeline(state->device, state->library, "uberCoreVertex",
                                        "uberCountingFragment", render::UberPipeline::replace);
    state->counting = buildPipeline(state->device, state->library, "uberVertex",
                                    "uberCountingFragment", render::UberPipeline::sourceOver);
    state->clear = buildPipeline(state->device, state->library, "clearVertex", "clearFragment",
                                 render::UberPipeline::replace);
    state->wavetable = buildPipeline(state->device, state->library, "wavetable3dVertex",
                                     "wavetable3dFragment", render::UberPipeline::sourceOver);
    if (!state->wavetable || !buildWavetableIndices(*state)) {
        return nullptr;
    }
    state->depthWrite = depthState(state->device, MTLCompareFunctionLess, YES);
    state->depthTest = depthState(state->device, MTLCompareFunctionLess, NO);
    state->depthReset = depthState(state->device, MTLCompareFunctionAlways, YES);
    state->reflect =
        [state->device newComputePipelineStateWithFunction:function(state->library, "reflectLayout")
                                                     error:&error];
    state->present =
        [state->device newComputePipelineStateWithFunction:function(state->library, "presentDither")
                                                     error:&error];
    const auto compute = [&](const char *name) {
        return [state->device newComputePipelineStateWithFunction:function(state->library, name)
                                                            error:nil];
    };
    MTLRenderPipelineDescriptor *presentation = [MTLRenderPipelineDescriptor new];
    presentation.vertexFunction = function(state->library, "clearVertex");
    presentation.fragmentFunction = function(state->library, "presentDrawableFragment");
    presentation.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
    state->presentDrawable = [state->device newRenderPipelineStateWithDescriptor:presentation
                                                                           error:&error];
    state->pathBinCount = compute("pathBinCount");
    state->pathScan = compute("pathScan");
    state->pathBatchScan = compute("pathBatchScan");
    state->pathBackdrop = compute("pathBackdrop");
    state->pathScatter = compute("pathScatter");
    state->pathFine = compute("pathFine");
    state->blurPaired = compute("blurPairedPass");
    state->blurShared = compute("blurSharedPass");
    state->kawaseDown = compute("kawaseDownPass");
    state->kawaseUp = compute("kawaseUpPass");
    state->mixTextures = compute("mixPass");
    state->effectComposite = compute("effectCompositePass");
    state->bloomPrefilter = compute("bloomPrefilterPass");
    state->bloomComposite = compute("bloomCompositePass");
    state->shadowProbe = compute("shadowProbe");
    state->formantColumns = compute("formantColumns");
    state->halftoneCells = compute("halftoneCells");
    state->shadowSprites = compute("shadowSprites");
    MTLTextureDescriptor *empty =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                           width:1
                                                          height:1
                                                       mipmapped:NO];
    state->emptyTexture = [state->device newTextureWithDescriptor:empty];
    const float zero[4] = {0, 0, 0, 0};
    [state->emptyTexture replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                           mipmapLevel:0
                             withBytes:zero
                           bytesPerRow:16];
    if (!state->opaque || !state->counting || !state->countingCore || !state->clear ||
        !state->reflect || !state->formantColumns || !state->halftoneCells ||
        !state->shadowSprites || !state->presentDrawable) {
        return nullptr;
    }
    return state;
}

MetalDevice::MetalDevice(std::shared_ptr<MetalDeviceState> state) : state_(std::move(state)) {}
MetalDevice::~MetalDevice() = default;

std::unique_ptr<hal::BackendDevice> MetalDevice::create() {
    @autoreleasepool {
        auto state = createMetalDeviceState();
        if (!state) {
            return nullptr;
        }
        return std::make_unique<MetalDevice>(std::move(state));
    }
}

std::unique_ptr<render::RenderSurface>
MetalDevice::createRenderSurface(const hal::SurfaceDesc &desc) {
    return std::make_unique<MetalSurface>(state_, desc);
}

std::unique_ptr<MetalSpectrogram> MetalDevice::createSpectrogram(math::WavetableView table,
                                                                 std::uint32_t rows) {
    return std::make_unique<MetalSpectrogram>(state_, table, rows);
}

bool MetalDevice::virtualized() const {
    @autoreleasepool {
        return [state_->device.name rangeOfString:@"Paravirtual"].location != NSNotFound;
    }
}

bool MetalDevice::reflectLayout(std::span<std::uint32_t, 16> output) const {
    @autoreleasepool {
        render::PrimitiveDesc probe{};
        probe.bounds[0] = 1.5f;
        probe.bounds[3] = -2.25f;
        probe.kindFlags = 0x12345;
        probe.color = 0xA1B2C3D4;
        probe.borderColor = 0x0F1E2D3C;
        probe.depth = 77;
        probe.transformId = 513;
        probe.clipId = 1027;
        probe.payloadOffset = 99;
        probe.params[0] = 3.75f;
        probe.params[3] = -0.5f;
        probe.auxHandle = 6;
        probe.gradientOffset = 42;
        id<MTLBuffer> input = [state_->device newBufferWithBytes:&probe
                                                          length:sizeof(probe)
                                                         options:MTLResourceStorageModeShared];
        id<MTLBuffer> result = [state_->device newBufferWithLength:16 * sizeof(std::uint32_t)
                                                           options:MTLResourceStorageModeShared];
        id<MTLCommandQueue> queue = [state_->device newCommandQueue];
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
        [encoder setComputePipelineState:state_->reflect];
        [encoder setBuffer:input offset:0 atIndex:0];
        [encoder setBuffer:result offset:0 atIndex:1];
        [encoder dispatchThreads:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
        [encoder endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        std::memcpy(output.data(), result.contents, 16 * sizeof(std::uint32_t));
        return commands.status == MTLCommandBufferStatusCompleted;
    }
}

bool MetalDevice::evaluateShadows(std::span<const ShadowQuery> queries,
                                  std::span<float> results) const {
    if (queries.size() != results.size() || queries.empty()) {
        return false;
    }
    @autoreleasepool {
        id<MTLBuffer> input = [state_->device newBufferWithBytes:queries.data()
                                                          length:queries.size_bytes()
                                                         options:MTLResourceStorageModeShared];
        id<MTLBuffer> output = [state_->device newBufferWithLength:results.size_bytes()
                                                           options:MTLResourceStorageModeShared];
        const auto count = static_cast<std::uint32_t>(queries.size());
        id<MTLCommandQueue> queue = [state_->device newCommandQueue];
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
        [encoder setComputePipelineState:state_->shadowProbe];
        [encoder setBuffer:input offset:0 atIndex:0];
        [encoder setBuffer:output offset:0 atIndex:1];
        [encoder setBytes:&count length:sizeof(count) atIndex:2];
        [encoder dispatchThreads:MTLSizeMake(count, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [encoder endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        std::memcpy(results.data(), output.contents, results.size_bytes());
        return commands.status == MTLCommandBufferStatusCompleted;
    }
}
}
