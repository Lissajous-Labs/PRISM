#pragma once

#include "dom/layout/LayoutSolvers.h"

#include <optional>
#include <span>
#include <vector>

namespace prism::dom::layout {
struct AnchoredItem {
    std::optional<float> left;
    std::optional<float> top;
    std::optional<float> right;
    std::optional<float> bottom;
    std::optional<float> width;
    std::optional<float> height;
};

[[nodiscard]] Placement solveAnchored(const AnchoredItem &item, float width, float height);

struct ScrollPlan {
    float extent;
    float reservation;
};

[[nodiscard]] ScrollPlan planScroll(std::span<const float> heightsAtFull, float viewportHeight,
                                    float gap, float scrollbarWidth);
void stackColumn(std::span<const float> heights, float gap, float offset,
                 std::vector<Placement> &out);
[[nodiscard]] float clampScroll(float requested, float extent, float viewportHeight);
}
