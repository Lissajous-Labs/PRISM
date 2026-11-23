#pragma once

#import <Metal/Metal.h>

#include "hal/HalTypes.h"
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

inline constexpr std::uint32_t zeroBandRows = 64;

struct TextureGrowth {
    std::size_t slot;
    id<MTLTexture> source;
    std::uint32_t copiedRows;
    std::uint32_t width;
    std::uint32_t height;
    std::size_t pixelBytes;
};

inline MTLPixelFormat nativeFormat(hal::PixelFormat format, std::size_t &pixelBytes) noexcept {
    switch (format) {
    case hal::PixelFormat::r8Unorm:
        pixelBytes = 1;
        return MTLPixelFormatR8Unorm;
    case hal::PixelFormat::r16Float:
        pixelBytes = 2;
        return MTLPixelFormatR16Float;
    case hal::PixelFormat::rgba16Float:
        pixelBytes = 8;
        return MTLPixelFormatRGBA16Float;
    case hal::PixelFormat::rg32Float:
        pixelBytes = 8;
        return MTLPixelFormatRG32Float;
    default:
        pixelBytes = 16;
        return MTLPixelFormatRGBA32Float;
    }
}

inline id<MTLTexture> makeTexture(id<MTLDevice> device, std::uint32_t width, std::uint32_t height,
                                  MTLPixelFormat format) {
    MTLTextureDescriptor *descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    return [device newTextureWithDescriptor:descriptor];
}

inline id<MTLTexture> uploadTexture(id<MTLDevice> device, id<MTLTexture> cached,
                                    std::uint32_t width, std::uint32_t height,
                                    std::uint32_t channels, std::span<const float> texels,
                                    std::vector<float> &scratch, hal::PixelFormat format) {
    if (width == 0 || height == 0 ||
        texels.size() < static_cast<std::size_t>(width) * height * channels) {
        return nil;
    }
    std::size_t pixelBytes = 16;
    const MTLPixelFormat nativeFormat = backends::nativeFormat(format, pixelBytes);
    if (cached == nil || cached.width != width || cached.height != height ||
        cached.pixelFormat != nativeFormat) {
        cached = makeTexture(device, width, height, nativeFormat);
    }
    const std::size_t count = static_cast<std::size_t>(width) * height;
    const void *bytes = texels.data();
    if (nativeFormat != MTLPixelFormatRGBA32Float || channels != 4) {
        scratch.resize((count * pixelBytes + 3) / 4);
        auto *out = reinterpret_cast<unsigned char *>(scratch.data());
        const std::size_t outputChannels =
            nativeFormat == MTLPixelFormatRG32Float ? 2
            : nativeFormat == MTLPixelFormatRGBA32Float || nativeFormat == MTLPixelFormatRGBA16Float
                ? 4
                : 1;
        for (std::size_t texel = 0; texel < count; ++texel) {
            for (std::size_t c = 0; c < outputChannels; ++c) {
                const float value =
                    c < channels ? texels[texel * channels + c] : (c == 3 ? 1.0f : 0.0f);
                const std::size_t index = texel * outputChannels + c;
                if (nativeFormat == MTLPixelFormatR8Unorm) {
                    out[index] =
                        static_cast<unsigned char>(std::clamp(value, 0.0f, 1.0f) * 255 + 0.5f);
                } else if (nativeFormat == MTLPixelFormatR16Float ||
                           nativeFormat == MTLPixelFormatRGBA16Float) {
                    const _Float16 half = static_cast<_Float16>(value);
                    std::memcpy(out + index * 2, &half, 2);
                } else {
                    std::memcpy(out + index * 4, &value, 4);
                }
            }
        }
        bytes = scratch.data();
    }
    [cached replaceRegion:MTLRegionMake2D(0, 0, width, height)
              mipmapLevel:0
                withBytes:bytes
              bytesPerRow:width * pixelBytes];
    return cached;
}

struct TileCopy {
    std::size_t slot;
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t width;
    std::uint32_t height;
    std::size_t offset;
    std::size_t rowBytes;
};

inline std::size_t stageTile(std::vector<unsigned char> &staging, std::uint32_t textureWidth,
                             std::uint32_t channels, std::span<const float> texels,
                             hal::PixelFormat format, std::uint32_t x, std::uint32_t y,
                             std::uint32_t width, std::uint32_t height, std::size_t &rowBytes) {
    const bool half =
        format == hal::PixelFormat::r16Float || format == hal::PixelFormat::rgba16Float;
    const std::size_t outputChannels =
        format == hal::PixelFormat::rg32Float                                         ? 2
        : format == hal::PixelFormat::r16Float || format == hal::PixelFormat::r8Unorm ? 1
                                                                                      : 4;
    const std::size_t channelBytes = format == hal::PixelFormat::r8Unorm ? 1 : half ? 2 : 4;
    rowBytes = width * outputChannels * channelBytes;
    const std::size_t offset = (staging.size() + 255) & ~std::size_t{255};
    staging.resize(offset + rowBytes * height);
    unsigned char *out = staging.data() + offset;

    for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t column = 0; column < width; ++column) {
            const std::size_t source =
                (static_cast<std::size_t>(y + row) * textureWidth + x + column) * channels;
            for (std::size_t c = 0; c < outputChannels; ++c) {
                const float value = c < channels ? texels[source + c] : (c == 3 ? 1.0f : 0.0f);
                unsigned char *target =
                    out + row * rowBytes + (column * outputChannels + c) * channelBytes;
                if (channelBytes == 1) {
                    *target =
                        static_cast<unsigned char>(std::clamp(value, 0.0f, 1.0f) * 255 + 0.5f);
                } else if (half) {
                    const _Float16 h = static_cast<_Float16>(value);
                    std::memcpy(target, &h, 2);
                } else {
                    std::memcpy(target, &value, 4);
                }
            }
        }
    }
    return offset;
}
}
