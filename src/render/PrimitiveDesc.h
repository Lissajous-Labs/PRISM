#pragma once

#include "hal/HalTypes.h"

#include <cstddef>
#include <cstdint>

namespace prism::render {
enum class PrimitiveKind : std::uint8_t {
    roundedRect = 0,
    squircle = 1,
    circleArc = 2,
    lineCapsule = 3,
    quadBezier = 4,
    triangle = 5,
    boxShadow = 6,
    glyphAlpha = 7,
    glyphLcd = 8,
    glyphMtsdf = 9,
    texturedQuad = 10,
    plotStrip = 11
};

inline constexpr std::uint32_t primitiveKindCount = 12;
enum class GradientShape : std::uint8_t { linear = 0, radial = 1, conic = 2 };

namespace flags {
inline constexpr std::uint32_t kindMask = 0xFFu;
inline constexpr std::uint32_t blendShift = 8;
inline constexpr std::uint32_t blendMask = 0x7u << blendShift;
inline constexpr std::uint32_t opaque = 1u << 11;
inline constexpr std::uint32_t border = 1u << 12;
inline constexpr std::uint32_t gradient = 1u << 13;
inline constexpr std::uint32_t capShift = 14;
inline constexpr std::uint32_t capMask = 0x3u << capShift;
inline constexpr std::uint32_t inset = 1u << 16;
inline constexpr std::uint32_t gradientShapeShift = 17;
inline constexpr std::uint32_t gradientShapeMask = 0x3u << gradientShapeShift;
}

struct alignas(64) PrimitiveDesc {
    float bounds[4];
    std::uint32_t kindFlags;
    std::uint32_t color;
    std::uint32_t borderColor;
    std::uint32_t depth;
    std::uint16_t transformId;
    std::uint16_t clipId;
    std::uint32_t payloadOffset;
    float params[4];
    std::uint32_t auxHandle;
    std::uint32_t gradientOffset;

    [[nodiscard]] constexpr PrimitiveKind kind() const noexcept {
        return static_cast<PrimitiveKind>(kindFlags & flags::kindMask);
    }
    [[nodiscard]] constexpr hal::BlendMode blend() const noexcept {
        return static_cast<hal::BlendMode>((kindFlags & flags::blendMask) >> flags::blendShift);
    }
    [[nodiscard]] constexpr bool has(std::uint32_t flag) const noexcept {
        return (kindFlags & flag) != 0;
    }
};

static_assert(sizeof(PrimitiveDesc) == 64);
static_assert(alignof(PrimitiveDesc) == 64);
static_assert(offsetof(PrimitiveDesc, kindFlags) == 16);
static_assert(offsetof(PrimitiveDesc, transformId) == 32);
static_assert(offsetof(PrimitiveDesc, params) == 40);
static_assert(offsetof(PrimitiveDesc, auxHandle) == 56);

[[nodiscard]] constexpr std::uint32_t packColor(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                                std::uint8_t a) noexcept {
    return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) |
           (static_cast<std::uint32_t>(b) << 16) | (static_cast<std::uint32_t>(a) << 24);
}
}
