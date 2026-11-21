#include "backends/metal/MetalFormantCache.h"

#include <cstring>

namespace prism::backends {
void MetalFormantCache::prepare(MetalDeviceState &device, const render::FrameView &frame,
                                id<MTLBuffer> primitives, hal::Extent extent) {
    count_ = 0;
    auto *gpuPrimitives = static_cast<render::PrimitiveDesc *>(primitives.contents);
    for (std::size_t i = 0; i < frame.primitives.size() && count_ < entries_.size(); ++i) {
        const auto &d = frame.primitives[i];
        if (d.kind() != render::PrimitiveKind::formantCurve) {
            continue;
        }
        const auto &t = frame.transforms[d.transformId];
        if (t.inverse[2] != 0) {
            continue;
        }
        auto &entry = entries_[count_];
        if (entry.texture == nil || entry.texture.width != extent.width) {
            auto *desc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                                   width:extent.width
                                                                  height:1
                                                               mipmapped:NO];
            desc.storageMode = MTLStorageModePrivate;
            desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
            entry.texture = [device.device newTextureWithDescriptor:desc];
            entry.dirty = true;
        }
        const auto *h = frame.payload.data() + d.payloadOffset;
        if (std::memcmp(&entry.payload, h, sizeof(entry.payload)) != 0 ||
            std::memcmp(&entry.transform, &t, sizeof(t)) != 0 || entry.dbRange != d.params[0]) {
            std::memcpy(&entry.payload, h, sizeof(entry.payload));
            entry.transform = t;
            entry.dbRange = d.params[0];
            entry.dirty = true;
        }
        entry.primitive = static_cast<std::uint32_t>(i);
        gpuPrimitives[i].params[1] = 1;
        gpuPrimitives[i].auxHandle = static_cast<std::uint32_t>(textureSlots + count_);
        ++count_;
    }
}
void MetalFormantCache::encode(MetalDeviceState &device, id<MTLCommandBuffer> commands,
                               id<MTLBuffer> primitives, id<MTLBuffer> payload,
                               id<MTLBuffer> transforms) {
    id<MTLComputeCommandEncoder> encoder = nil;
    for (std::size_t i = 0; i < count_; ++i) {
        auto &entry = entries_[i];
        if (!entry.dirty) {
            continue;
        }
        if (encoder == nil) {
            encoder = [commands computeCommandEncoder];
            [encoder setComputePipelineState:device.formantColumns];
            [encoder setBuffer:primitives offset:0 atIndex:0];
            [encoder setBuffer:payload offset:0 atIndex:1];
            [encoder setBuffer:transforms offset:0 atIndex:2];
        }
        [encoder setBytes:&entry.primitive length:sizeof(entry.primitive) atIndex:3];
        [encoder setTexture:entry.texture atIndex:0];
        [encoder dispatchThreads:MTLSizeMake(entry.texture.width, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        entry.dirty = false;
    }
    [encoder endEncoding];
}
}
