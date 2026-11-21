#pragma once

#include "render/FrameRecords.h"
#include "render/PrimitiveDesc.h"

#include <cstdint>
#include <span>

namespace prism::render {
inline constexpr std::uint32_t gradientRowTexels = 256;

struct TextureView {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t channels = 4;
    std::span<const float> texels;
};

struct FrameView {
    std::span<const PrimitiveDesc> primitives;
    std::span<const float> payload;
    std::span<const TransformRecord> transforms;
    std::span<const ClipRecord> clips;
    std::span<const float> gradientRows;
    std::span<const TextureView> textures;
};
}
