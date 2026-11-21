#include "studio/GpuProfilerHud.h"

#include <algorithm>

namespace prism::studio {
void GpuProfilerHud::draw(render::DrawList &list, render::RectF rect, double budgetMs) {
    const float width = rect.x1 - rect.x0, height = rect.y1 - rect.y0;
    list.roundedRect(rect, {4, 4, 4, 4}, render::packColor(10, 12, 16, 220));
    const auto columns = std::min<std::size_t>(gpu_.size(), static_cast<std::size_t>(width / 2));
    const auto scale = [&](double ms) {
        return static_cast<float>(std::clamp(ms / (2 * budgetMs), 0.0, 1.0)) * height;
    };
    for (std::size_t i = 0; i < columns; ++i) {
        const float x1 = rect.x1 - 2 * static_cast<float>(i);
        const float hostHeight = scale(host_.recent(i));
        const float gpuHeight = scale(gpu_.recent(i));
        const float base = rect.y1;
        list.roundedRect({x1 - 1.5f, base - hostHeight, x1, base}, {},
                         render::packColor(90, 160, 255, 230));
        list.roundedRect({x1 - 1.5f, base - hostHeight - gpuHeight, x1, base - hostHeight}, {},
                         render::packColor(255, 150, 70, 230));
    }
    const float budget = rect.y1 - scale(budgetMs);
    list.roundedRect({rect.x0, budget - 0.5f, rect.x1, budget + 0.5f}, {},
                     render::packColor(240, 240, 240, 160));
    const float p99 = rect.y1 - scale(gpu_.quantile(0.99) + host_.quantile(0.99));
    list.roundedRect({rect.x0, p99 - 0.5f, rect.x0 + 0.25f * width, p99 + 0.5f}, {},
                     render::packColor(255, 80, 60, 220));
}
}
