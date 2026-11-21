#pragma once

#include "text/FontFile.h"
#include "text/GlyphBitmap.h"

namespace prism::text {
inline constexpr std::uint32_t subpixelPhases = 4;

void rasterizeAlpha(const FontFile &font, std::uint16_t glyph, float pixelSize, std::uint32_t phase,
                    GlyphBitmap &bitmap);
void rasterizeLcd(const FontFile &font, std::uint16_t glyph, float pixelSize, std::uint32_t phase,
                  GlyphBitmap &bitmap);
}
