#include "widgets/GpuLoudnessMeter.h"

#include <algorithm>

namespace prism::widgets {
void drawLoudnessMeter(render::DrawList &list, render::RectF rect,
                       const telemetry::LoudnessReading &reading, float peakHold,
                       const MeterStyle &style) {
    const float height = rect.y1 - rect.y0;
    const auto yOf = [&](float db) {
        const float t =
            std::clamp((db - style.minimum) / (style.maximum - style.minimum), 0.0f, 1.0f);
        return rect.y1 - t * height;
    };
    list.roundedRect(rect, {2, 2, 2, 2}, style.background);
    const float barTop = yOf(reading.momentary);
    if (barTop < rect.y1) {
        list.roundedRect({rect.x0 + 2, barTop, rect.x1 - 2, rect.y1 - 1}, {1, 1, 1, 1}, style.bar);
    }
    for (float db = style.maximum; db >= style.minimum; db -= style.tickStep) {
        const float y = yOf(db);
        list.roundedRect({rect.x0, y - 0.5f, rect.x0 + 4, y + 0.5f}, {}, style.tick);
    }
    const float hold = yOf(std::max(peakHold, reading.truePeak));
    list.roundedRect({rect.x0 + 1, hold - 1, rect.x1 - 1, hold + 1}, {}, style.peak);
}
}
