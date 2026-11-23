#pragma once

#include "render/SlugCoverage.h"
#include "render/vector/VectorPath.h"

#include <vector>

namespace prism::text {
using render::slugCoverage;
using render::slugCurveFloats;
using render::slugHeader;
using render::slugMaxBands;

struct SlugGlyph {
    float x0 = 0;
    float y0 = 0;
    float x1 = 0;
    float y1 = 0;
    std::vector<float> blob;
};

[[nodiscard]] SlugGlyph buildSlugGlyph(const render::VectorPath &outline, double tolerance);
}
