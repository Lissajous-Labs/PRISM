#pragma once

#include "render/DrawList.h"
#include "telemetry/LoudnessMeter.h"

#include <cstdint>

namespace prism::widgets {
struct MeterStyle {
    float minimum = -60;
    float maximum = 0;
    float tickStep = 6;
    std::uint32_t background;
    std::uint32_t bar;
    std::uint32_t tick;
    std::uint32_t peak;
};

void drawLoudnessMeter(render::DrawList &list, render::RectF rect,
                       const telemetry::LoudnessReading &reading, float peakHold,
                       const MeterStyle &style);
}
