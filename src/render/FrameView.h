#pragma once

#include "render/FrameRecords.h"
#include "render/PrimitiveDesc.h"
#include "render/effects/EffectDraw.h"
#include "render/vector/PathDraw.h"

#include <cstdint>
#include <span>

namespace prism::render {
inline constexpr std::uint32_t gradientRowTexels = 256;

struct TextureView {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t channels = 4;
    std::span<const float> texels;
    hal::PixelFormat format = hal::PixelFormat::rgba32Float;
    std::uint64_t generation = 0;
    void *nativeTexture = nullptr;
    std::span<const std::uint64_t> tiles{};
    std::uint32_t tileSize = 0;
};

struct FrameView {
    std::span<const PrimitiveDesc> primitives;
    std::span<const float> payload;
    std::span<const TransformRecord> transforms;
    std::span<const ClipRecord> clips;
    std::span<const float> gradientRows;
    std::span<const TextureView> textures;
    std::span<const PathDraw> paths = {};
    std::span<const LineSegment> pathSegments = {};
    std::span<const EffectDraw> effects = {};
};
}
