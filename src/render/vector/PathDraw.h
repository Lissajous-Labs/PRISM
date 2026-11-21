#pragma once

#include "render/vector/PathTypes.h"

#include <cstdint>

namespace prism::render {
struct PathDraw {
    std::uint32_t firstSegment;
    std::uint32_t segmentCount;
    std::uint32_t color;
    std::uint32_t insertAfter;
    std::uint16_t clipId;
    FillRule rule;
    std::uint8_t reserved;
};

static_assert(sizeof(PathDraw) == 20);
}
