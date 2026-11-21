#pragma once

#import <Metal/Metal.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace prism::backends {
inline float halfToFloat(std::uint16_t half) noexcept {
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

inline void upload(id<MTLDevice> device, id<MTLBuffer> __strong &buffer, const void *data,
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

inline id<MTLTexture> uploadTexture(id<MTLDevice> device, id<MTLTexture> cached,
                                    std::uint32_t width, std::uint32_t height,
                                    std::uint32_t channels, std::span<const float> texels,
                                    std::vector<float> &scratch) {
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
