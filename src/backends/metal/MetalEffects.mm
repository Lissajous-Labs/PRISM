#include "backends/metal/MetalEffects.h"
#include "backends/metal/MetalUpload.h"
#include "render/effects/BackdropBlur.h"
#include "render/effects/DualKawase.h"
#include "render/effects/GaussianBlur.h"

namespace prism::backends {
namespace {
struct EffectUniforms {
    std::int32_t crop[4];
    float rect[4];
    float radii[4];
    float sourceSize[2];
    float targetSize[2];
    std::uint32_t tapCount;
    std::uint32_t horizontal;
    float intensity;
    float blend;
    float threshold;
    float knee;
    std::uint32_t karis;
    std::uint32_t reserved;
};

static_assert(sizeof(EffectUniforms) == 96);

EffectUniforms sized(id<MTLTexture> source, id<MTLTexture> target) {
    EffectUniforms u{};
    u.sourceSize[0] = static_cast<float>(source.width);
    u.sourceSize[1] = static_cast<float>(source.height);
    u.targetSize[0] = static_cast<float>(target.width);
    u.targetSize[1] = static_cast<float>(target.height);
    return u;
}

void dispatchPixels(id<MTLComputeCommandEncoder> encoder, NSUInteger width, NSUInteger height) {
    [encoder dispatchThreads:MTLSizeMake(width, height, 1)
        threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
}

void run(id<MTLCommandBuffer> commands, id<MTLComputePipelineState> pipeline, id<MTLTexture> source,
         id<MTLTexture> target, const EffectUniforms &u) {
    id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
    [encoder setComputePipelineState:pipeline];
    [encoder setTexture:source atIndex:0];
    [encoder setTexture:target atIndex:1];
    [encoder setBytes:&u length:sizeof(u) atIndex:1];
    dispatchPixels(encoder, target.width, target.height);
    [encoder endEncoding];
}
}

id<MTLTexture> MetalEffects::texture(MetalDeviceState &device, std::uint32_t width,
                                     std::uint32_t height, std::uint32_t slot) {
    for (const PooledTexture &entry : pool_) {
        if (entry.width == width && entry.height == height && entry.slot == slot) {
            return entry.texture;
        }
    }
    MTLTextureDescriptor *descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    descriptor.storageMode = MTLStorageModePrivate;
    id<MTLTexture> created = [device.device newTextureWithDescriptor:descriptor];
    pool_.push_back({width, height, slot, created});
    return created;
}

id<MTLTexture> MetalEffects::kawaseChain(MetalDeviceState &device, id<MTLCommandBuffer> commands,
                                         id<MTLTexture> source, int levels,
                                         std::uint32_t slotBase) {
    std::vector<id<MTLTexture>> chain{source};
    for (int level = 0; level < levels; ++level) {
        id<MTLTexture> previous = chain.back();
        id<MTLTexture> next = texture(
            device, std::max<std::uint32_t>(1, static_cast<std::uint32_t>(previous.width) / 2),
            std::max<std::uint32_t>(1, static_cast<std::uint32_t>(previous.height) / 2),
            slotBase + level);
        run(commands, device.kawaseDown, previous, next, sized(previous, next));
        chain.push_back(next);
    }
    id<MTLTexture> current = chain.back();
    for (int level = levels - 1; level >= 0; --level) {
        id<MTLTexture> target =
            texture(device, static_cast<std::uint32_t>(chain[level].width),
                    static_cast<std::uint32_t>(chain[level].height), slotBase + 32 + level);
        run(commands, device.kawaseUp, current, target, sized(current, target));
        current = target;
    }
    return current;
}

void MetalEffects::backdrop(MetalDeviceState &device, id<MTLCommandBuffer> commands,
                            id<MTLTexture> color, const render::EffectDraw &effect,
                            hal::Extent extent) {
    const hal::Rect crop = render::effectCrop(effect, extent.width, extent.height);
    if (crop.empty()) {
        return;
    }
    const auto width = static_cast<std::uint32_t>(crop.x1 - crop.x0);
    const auto height = static_cast<std::uint32_t>(crop.y1 - crop.y0);
    id<MTLTexture> source = texture(device, width, height, 0);
    id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
    [blit copyFromTexture:color
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(crop.x0, crop.y0, 0)
               sourceSize:MTLSizeMake(width, height, 1)
                toTexture:source
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    id<MTLTexture> blurred = nil;
    switch (render::blurMethodFor(effect.sigma)) {
    case render::BlurMethod::paired: {
        const auto taps = render::pairedTaps(render::gaussianWeights(effect.sigma));
        upload(device.device, taps_, taps.data(), taps.size() * sizeof(render::PairedTap));
        id<MTLTexture> middle = texture(device, width, height, 1);
        blurred = texture(device, width, height, 2);
        for (const bool horizontal : {true, false}) {
            EffectUniforms u = sized(source, middle);
            u.tapCount = static_cast<std::uint32_t>(taps.size());
            u.horizontal = horizontal ? 1 : 0;
            id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
            [encoder setComputePipelineState:device.blurPaired];
            [encoder setTexture:(horizontal ? source : middle) atIndex:0];
            [encoder setTexture:(horizontal ? middle : blurred) atIndex:1];
            [encoder setBuffer:taps_ offset:0 atIndex:0];
            [encoder setBytes:&u length:sizeof(u) atIndex:1];
            dispatchPixels(encoder, width, height);
            [encoder endEncoding];
        }
        break;
    }
    case render::BlurMethod::separable: {
        const auto weights = render::gaussianWeights(effect.sigma);
        upload(device.device, taps_, weights.data(), weights.size() * sizeof(float));
        id<MTLTexture> middle = texture(device, width, height, 1);
        blurred = texture(device, width, height, 2);
        for (const bool horizontal : {true, false}) {
            EffectUniforms u = sized(source, middle);
            u.tapCount = static_cast<std::uint32_t>(weights.size());
            u.horizontal = horizontal ? 1 : 0;
            id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
            [encoder setComputePipelineState:device.blurShared];
            [encoder setTexture:(horizontal ? source : middle) atIndex:0];
            [encoder setTexture:(horizontal ? middle : blurred) atIndex:1];
            [encoder setBuffer:taps_ offset:0 atIndex:0];
            [encoder setBytes:&u length:sizeof(u) atIndex:1];
            const NSUInteger along = horizontal ? width : height;
            const NSUInteger across = horizontal ? height : width;
            [encoder dispatchThreadgroups:MTLSizeMake((along + 255) / 256, across, 1)
                    threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
            [encoder endEncoding];
        }
        break;
    }
    case render::BlurMethod::kawase: {
        const auto levels = render::kawaseLevels(effect.sigma);
        id<MTLTexture> lower = kawaseChain(device, commands, source, levels.levels, 100);
        blurred = lower;
        if (levels.blend > 0) {
            id<MTLTexture> upper = kawaseChain(device, commands, source, levels.levels + 1, 200);
            blurred = texture(device, width, height, 3);
            EffectUniforms u = sized(lower, blurred);
            u.blend = levels.blend;
            id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
            [encoder setComputePipelineState:device.mixTextures];
            [encoder setTexture:lower atIndex:0];
            [encoder setTexture:upper atIndex:1];
            [encoder setTexture:blurred atIndex:2];
            [encoder setBytes:&u length:sizeof(u) atIndex:1];
            dispatchPixels(encoder, width, height);
            [encoder endEncoding];
        }
        break;
    }
    }
    EffectUniforms u{};
    u.crop[0] = crop.x0;
    u.crop[1] = crop.y0;
    u.crop[2] = crop.x1;
    u.crop[3] = crop.y1;
    for (int i = 0; i < 4; ++i) {
        u.rect[i] = effect.rect[i];
        u.radii[i] = effect.radii[i];
    }
    id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
    [encoder setComputePipelineState:device.effectComposite];
    [encoder setTexture:blurred atIndex:0];
    [encoder setTexture:color atIndex:1];
    [encoder setBytes:&u length:sizeof(u) atIndex:1];
    dispatchPixels(encoder, width, height);
    [encoder endEncoding];
}

void MetalEffects::bloom(MetalDeviceState &device, id<MTLCommandBuffer> commands,
                         id<MTLTexture> scene, id<MTLTexture> output,
                         const render::BloomSettings &settings) {
    id<MTLTexture> prefiltered =
        texture(device, std::max<std::uint32_t>(1, static_cast<std::uint32_t>(scene.width) / 2),
                std::max<std::uint32_t>(1, static_cast<std::uint32_t>(scene.height) / 2), 300);
    EffectUniforms u = sized(scene, prefiltered);
    u.threshold = settings.threshold;
    u.knee = settings.knee;
    u.karis = settings.karis ? 1 : 0;
    run(commands, device.bloomPrefilter, scene, prefiltered, u);
    id<MTLTexture> glow = kawaseChain(device, commands, prefiltered, settings.levels, 400);
    EffectUniforms c = sized(glow, output);
    c.intensity = settings.intensity;
    id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
    [encoder setComputePipelineState:device.bloomComposite];
    [encoder setTexture:scene atIndex:0];
    [encoder setTexture:glow atIndex:1];
    [encoder setTexture:output atIndex:2];
    [encoder setBytes:&c length:sizeof(c) atIndex:1];
    dispatchPixels(encoder, output.width, output.height);
    [encoder endEncoding];
}
}
