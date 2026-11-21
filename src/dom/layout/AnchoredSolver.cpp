#include "dom/layout/AnchoredSolver.h"

#include <algorithm>

namespace prism::dom::layout {
namespace {
void solveAxis(std::optional<float> start, std::optional<float> end, std::optional<float> size,
               float extent, float &outStart, float &outEnd) {
    if (size) {
        if (start) {
            outStart = *start;
        } else if (end) {
            outStart = extent - *end - *size;
        } else {
            outStart = 0;
        }
        outEnd = outStart + *size;
        return;
    }
    outStart = start.value_or(0);
    outEnd = std::max(outStart, extent - end.value_or(0));
}
}

Placement solveAnchored(const AnchoredItem &item, float width, float height) {
    Placement placement{};
    solveAxis(item.left, item.right, item.width, width, placement.mainStart, placement.mainEnd);
    solveAxis(item.top, item.bottom, item.height, height, placement.crossStart, placement.crossEnd);
    return placement;
}

ScrollPlan planScroll(std::span<const float> heightsAtFull, float viewportHeight, float gap,
                      float scrollbarWidth) {
    float extent = 0;
    for (std::size_t i = 0; i < heightsAtFull.size(); ++i) {
        extent += heightsAtFull[i] + (i > 0 ? gap : 0);
    }
    return {extent, extent > viewportHeight ? scrollbarWidth : 0.0f};
}

void stackColumn(std::span<const float> heights, float gap, float offset,
                 std::vector<Placement> &out) {
    out.resize(heights.size());
    float cursor = -offset;
    for (std::size_t i = 0; i < heights.size(); ++i) {
        out[i].crossStart = cursor;
        out[i].crossEnd = cursor + heights[i];
        cursor = out[i].crossEnd + gap;
    }
}

float clampScroll(float requested, float extent, float viewportHeight) {
    return std::clamp(requested, 0.0f, std::max(0.0f, extent - viewportHeight));
}
}
