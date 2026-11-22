#pragma once

#include "render/vector/VectorPath.h"
#include "text/FontFile.h"
#include "text/GlyphBitmap.h"

namespace prism::text {
struct MtsdfSettings {
    double emSize = 48;
    double spread = 6;
};

void generateMtsdf(const render::VectorPath &outline, const MtsdfSettings &settings,
                   GlyphBitmap &bitmap);
void generateMtsdf(const FontFile &font, std::uint16_t glyph, const MtsdfSettings &settings,
                   GlyphBitmap &bitmap);
}
