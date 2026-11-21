#pragma once

#include <cstdint>

namespace prism::render {
enum class FillRule : std::uint8_t { nonZero, evenOdd };
enum class LineJoin : std::uint8_t { miter, round, bevel };
enum class LineCap : std::uint8_t { butt, round, square };

struct LineSegment {
    float x0;
    float y0;
    float x1;
    float y1;
};
}
