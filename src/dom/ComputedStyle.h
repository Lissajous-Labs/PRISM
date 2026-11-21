#pragma once

#include "dom/StyleSheet.h"

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace prism::dom {
struct alignas(16) ColorBlock {
    std::uint32_t color = 0x000000FF;
    std::uint32_t background = 0;
    std::uint32_t borderColor = 0;
    float opacity = 1;
    float borderRadius = 0;
    std::uint32_t reserved[3]{};
};

struct alignas(16) TransformBlock {
    float translateX = 0;
    float translateY = 0;
    float scale = 1;
    float rotate = 0;
};

struct alignas(16) LayoutBlock {
    Length width{0, Unit::automatic, {}};
    Length height{0, Unit::automatic, {}};
    Length minWidth{};
    Length minHeight{};
    Length maxWidth{0, Unit::automatic, {}};
    Length maxHeight{0, Unit::automatic, {}};
    Length margin{};
    Length padding{};
    Length gap{};
    Length flexBasis{0, Unit::automatic, {}};
    Length left{0, Unit::automatic, {}};
    Length top{0, Unit::automatic, {}};
    Length right{0, Unit::automatic, {}};
    Length bottom{0, Unit::automatic, {}};
    float borderWidth = 0;
    float flexGrow = 0;
    float flexShrink = 1;
    float fontSize = 14;
    float lineHeight = 0;
    std::int32_t order = 0;
    Keyword display = Keyword::flex;
    Keyword flexDirection = Keyword::row;
    Keyword flexWrap = Keyword::nowrap;
    Keyword justifyContent = Keyword::start;
    Keyword alignItems = Keyword::stretch;
    Keyword alignSelf = Keyword::automatic;
    Keyword visibility = Keyword::visible;
    std::uint16_t reserved = 0;
    std::uint32_t tail[2]{};
};

struct alignas(16) MotionBlock {
    std::array<Property, 4> properties{Property::count, Property::count, Property::count,
                                       Property::count};
    std::array<float, 4> omega{};
    std::uint32_t count = 0;
    std::uint32_t reserved = 0;
};

static_assert(sizeof(ColorBlock) == 32 && sizeof(TransformBlock) == 16);
static_assert(sizeof(LayoutBlock) == 160 && sizeof(MotionBlock) == 32);
static_assert(std::is_trivially_copyable_v<LayoutBlock> &&
              std::is_trivially_copyable_v<MotionBlock>);

struct ComputedStyle {
    ColorBlock colors;
    TransformBlock transform;
    LayoutBlock layout;
    MotionBlock motion;
};

namespace diff {
inline constexpr std::uint8_t none = 0, colorOnly = 1, transformOnly = 2, layout = 4, structure = 8;
}

template <class Block>
[[nodiscard]] inline bool blocksEqual(const Block &a, const Block &b) noexcept {
    static_assert(sizeof(Block) % 16 == 0);
    std::uint64_t folded = 0;
    for (std::size_t offset = 0; offset < sizeof(Block); offset += 16) {
        std::uint64_t x[2];
        std::uint64_t y[2];
        std::memcpy(x, reinterpret_cast<const unsigned char *>(&a) + offset, 16);
        std::memcpy(y, reinterpret_cast<const unsigned char *>(&b) + offset, 16);
        folded |= (x[0] ^ y[0]) | (x[1] ^ y[1]);
    }
    return folded == 0;
}

[[nodiscard]] inline std::uint8_t styleDiff(const ComputedStyle &before,
                                            const ComputedStyle &after) noexcept {
    std::uint8_t mask = diff::none;
    mask |= blocksEqual(before.colors, after.colors) && blocksEqual(before.motion, after.motion)
                ? 0
                : diff::colorOnly;
    mask |= blocksEqual(before.transform, after.transform) ? 0 : diff::transformOnly;
    mask |= blocksEqual(before.layout, after.layout) ? 0 : diff::layout;
    return mask;
}
}
