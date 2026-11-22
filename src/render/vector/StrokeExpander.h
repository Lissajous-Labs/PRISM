#pragma once

#include "render/vector/PathTypes.h"
#include "render/vector/VectorPath.h"

#include <span>
#include <vector>

namespace prism::render {
struct StrokeStyle {
    double width = 1;
    LineJoin join = LineJoin::miter;
    LineCap cap = LineCap::butt;
    double miterLimit = 4;
    std::span<const double> dashes;
    double dashOffset = 0;
    double tolerance = 0.125;
};

struct StrokeScratch {
    PolylineBuffer dashed;
    std::vector<math::Vector2> cleaned;
    std::vector<math::Vector2> reversed;
};

void dashPolylines(std::span<const Polyline> input, std::span<const double> dashes, double offset,
                   PolylineBuffer &output);
void strokeOutlines(std::span<const Polyline> input, const StrokeStyle &style,
                    StrokeScratch &scratch, PolylineBuffer &output);
}
