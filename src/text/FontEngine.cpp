#include "text/FontEngine.h"

#include "math/Hash64.h"
#include "text/GlyphRasterizer.h"

#include <cmath>

namespace prism::text {
FontEngine::FontEngine(std::vector<const FontFile *> fonts, std::uint32_t textureBase,
                       std::uint32_t pageSize)
    : fonts_(std::move(fonts)), textureBase_(textureBase),
      atlases_{GlyphAtlas(pageSize, 1, 1), GlyphAtlas(pageSize, 4, 1), GlyphAtlas(pageSize, 4, 1)} {
}

TextTier FontEngine::tierFor(float physicalSize, const TextStyle &style) noexcept {
    if (style.transformed || physicalSize > tierOneMaximumPixels) {
        return TextTier::mtsdf;
    }
    return style.subpixel ? TextTier::lcd : TextTier::alpha;
}

std::optional<FontEngine::Placement> FontEngine::glyphFor(TextTier tier,
                                                          const PositionedGlyph &glyph,
                                                          float pixelSize, std::uint32_t phase) {
    const auto quantizedSize = static_cast<std::uint64_t>(std::lround(pixelSize * 8));
    std::uint64_t key = (static_cast<std::uint64_t>(glyph.font) << 56) ^
                        (static_cast<std::uint64_t>(glyph.glyph) << 40) ^
                        (static_cast<std::uint64_t>(tier) << 36);
    if (tier != TextTier::mtsdf) {
        key ^= (quantizedSize << 8) ^ phase;
    }
    key = math::mix64(key);
    GlyphAtlas &atlas = atlases_[static_cast<std::size_t>(tier)];
    const FontFile &font = *fonts_[glyph.font];
    if (const auto found = atlas.find(key, frame_)) {
        const Placement &placement = placements_.at(key);
        return Placement{*found, placement.left, placement.top, placement.scale};
    }
    if (emptyGlyphs_.contains(key)) {
        return std::nullopt;
    }
    switch (tier) {
    case TextTier::alpha:
        rasterizeAlpha(font, glyph.glyph, pixelSize, phase, scratch_);
        break;
    case TextTier::lcd:
        rasterizeLcd(font, glyph.glyph, pixelSize, phase, scratch_);
        break;
    case TextTier::mtsdf:
        generateMtsdf(font, glyph.glyph, mtsdf_, scratch_);
        break;
    }
    ++rasterizations_;
    if (scratch_.width == 0) {
        emptyGlyphs_.insert(key);
        return std::nullopt;
    }
    const auto slot = atlas.insert(key, scratch_.width, scratch_.height, scratch_.texels, frame_);
    if (!slot) {
        return std::nullopt;
    }
    Placement placement{*slot, scratch_.left, scratch_.top,
                        tier == TextTier::mtsdf ? static_cast<float>(mtsdf_.emSize) : pixelSize};
    placements_[key] = placement;
    refreshViews();
    return placement;
}

void FontEngine::refreshViews() {
    for (std::size_t i = 0; i < atlases_.size(); ++i) {
        const GlyphAtlas &atlas = atlases_[i];
        if (atlas.pageCount() != 0) {
            views_[i] = {atlas.pageSize(), atlas.pageSize(), atlas.channels(), atlas.page(0)};
            views_[i].generation = rasterizations_ + 1;
        }
    }
}

void FontEngine::draw(render::DrawList &list, const TextLayout &layout, math::Vector2 origin,
                      const TextStyle &style, float textSize, double physicalScale) {
    const float physicalSize = static_cast<float>(textSize * physicalScale);
    const TextTier tier = tierFor(physicalSize, style);
    const auto kind = tier == TextTier::alpha ? render::PrimitiveKind::glyphAlpha
                      : tier == TextTier::lcd ? render::PrimitiveKind::glyphLcd
                                              : render::PrimitiveKind::glyphMtsdf;
    const std::uint32_t texture = textureBase_ + static_cast<std::uint32_t>(tier);
    for (const PositionedGlyph &glyph : layout.glyphs) {
        const double physicalX = (origin.x + glyph.x) * physicalScale;
        const double physicalY = (origin.y + glyph.y) * physicalScale;
        double penX = physicalX;
        std::uint32_t phase = 0;
        if (tier != TextTier::mtsdf) {
            const double whole = std::floor(physicalX);
            phase = static_cast<std::uint32_t>(std::floor((physicalX - whole) * subpixelPhases)) %
                    subpixelPhases;
            penX = whole;
        }
        const auto placement = glyphFor(tier, glyph, physicalSize, phase);
        if (!placement) {
            continue;
        }
        const GlyphAtlas &atlas = atlases_[static_cast<std::size_t>(tier)];
        const float page = static_cast<float>(atlas.pageSize());
        const render::RectF uv{placement->slot.x / page, placement->slot.y / page,
                               (placement->slot.x + placement->slot.width) / page,
                               (placement->slot.y + placement->slot.height) / page};
        double x0;
        double y0;
        double unit;
        if (tier == TextTier::mtsdf) {
            unit = physicalSize / placement->scale;
            x0 = physicalX + placement->left * unit;
            y0 = physicalY + placement->top * unit;
        } else {
            unit = 1;
            x0 = penX + placement->left;
            y0 = std::floor(physicalY) + placement->top;
        }
        const double x1 = x0 + placement->slot.width * unit;
        const double y1 = y0 + placement->slot.height * unit;
        list.texturedQuad(
            kind,
            {static_cast<float>(x0 / physicalScale), static_cast<float>(y0 / physicalScale),
             static_cast<float>(x1 / physicalScale), static_cast<float>(y1 / physicalScale)},
            uv, texture, style.color, static_cast<float>(2 * mtsdf_.spread));
    }
}
}
