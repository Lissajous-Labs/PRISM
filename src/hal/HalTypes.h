#pragma once

#include <cstdint>

namespace prism::hal {
enum class PixelFormat : std::uint8_t {
    r8Unorm,
    r16Float,
    rg32Float,
    rgba8Unorm,
    rgba8Srgb,
    bgra8Srgb,
    rgba16Float,
    rgba32Float,
    depth32Float
};

enum class BackendKind : std::uint8_t { cpuReference, metal };

enum class BlendMode : std::uint8_t { sourceOver, screen, multiply, additive, replace };

struct Extent {
    std::uint32_t width;
    std::uint32_t height;
    friend constexpr bool operator==(const Extent &, const Extent &) = default;
};

[[nodiscard]] constexpr std::uint32_t bytesPerPixel(PixelFormat format) noexcept {
    switch (format) {
    case PixelFormat::r8Unorm:
        return 1;
    case PixelFormat::r16Float:
        return 2;
    case PixelFormat::rgba8Unorm:
    case PixelFormat::rgba8Srgb:
    case PixelFormat::bgra8Srgb:
    case PixelFormat::depth32Float:
        return 4;
    case PixelFormat::rg32Float:
    case PixelFormat::rgba16Float:
        return 8;
    case PixelFormat::rgba32Float:
        return 16;
    }
    return 0;
}

[[nodiscard]] constexpr std::uint64_t textureBytes(Extent extent, PixelFormat format) noexcept {
    return static_cast<std::uint64_t>(extent.width) * extent.height * bytesPerPixel(format);
}
}
