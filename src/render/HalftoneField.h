#pragma once

#include "math/Color.h"
#include "math/HalftoneLattice.h"
#include "render/FrameView.h"

#include <cstdint>

namespace prism::render {
namespace halftoneFlags {
inline constexpr std::uint32_t spectralMatrix = 1;
inline constexpr std::uint32_t hexagonal = 2;
inline constexpr std::uint32_t superellipse = 4;
inline constexpr std::uint32_t cursor = 8;
inline constexpr std::uint32_t summedArea = 16;
inline constexpr std::uint32_t cleanBackdrop = 32;
inline constexpr std::uint32_t pairedSpots = 64;
}
struct alignas(16) HalftonePayload {
    float rect[4];
    float basis[4];
    float invBasis[4];
    float origin[2];
    float gutter[2];
    float spotParams[4];
    float screenParams[4];
    float cursorParams[4];
    std::uint32_t modeFlags;
    std::uint32_t paletteRow;
    std::uint32_t blueNoiseSlot;
    std::uint32_t reserved;
};
static_assert(sizeof(HalftonePayload) == 128);
[[nodiscard]] math::PremultipliedColor shadeHalftone(const FrameView &frame,
                                                     const PrimitiveDesc &desc,
                                                     const TransformRecord &transform,
                                                     math::Vector2 local) noexcept;
}
