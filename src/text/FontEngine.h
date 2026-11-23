#pragma once

#include "render/DrawList.h"
#include "render/FrameView.h"
#include "text/GlyphAtlas.h"
#include "text/GlyphBitmap.h"
#include "text/MtsdfGenerator.h"
#include "text/SlugGlyph.h"
#include "text/TextShaper.h"

#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace prism::text {
enum class TextTier : std::uint8_t { alpha, lcd, mtsdf, slug };

struct TextStyle {
    std::uint32_t color;
    bool subpixel = false;
    bool transformed = false;
    float background = -1;
};

inline constexpr float tierOneMaximumPixels = 14;
inline constexpr float tierTwoMaximumPixels = 48;

class FontEngine {
  public:
    FontEngine(std::vector<const FontFile *> fonts, std::uint32_t textureBase,
               std::uint32_t pageSize = 1024);

    void beginFrame() noexcept { ++frame_; }
    void draw(render::DrawList &list, const TextLayout &layout, math::Vector2 origin,
              const TextStyle &style, float textSize, double physicalScale);

    [[nodiscard]] static TextTier tierFor(float physicalSize, const TextStyle &style) noexcept;
    [[nodiscard]] std::span<const render::TextureView> textures() const noexcept { return views_; }
    [[nodiscard]] std::size_t rasterizations() const noexcept { return rasterizations_; }
    [[nodiscard]] std::size_t outlines() const noexcept { return slugs_.size(); }
    [[nodiscard]] const GlyphAtlas &atlas(TextTier tier) const noexcept {
        return atlases_[static_cast<std::size_t>(tier)];
    }

  private:
    std::vector<const FontFile *> fonts_;
    std::uint32_t textureBase_;
    std::array<GlyphAtlas, 3> atlases_;
    std::array<render::TextureView, 3> views_{};
    std::uint64_t frame_ = 0;
    std::size_t rasterizations_ = 0;
    GlyphBitmap scratch_;
    MtsdfSettings mtsdf_;

    struct Placement {
        AtlasSlot slot;
        float left;
        float top;
        float scale;
    };
    std::unordered_map<std::uint64_t, Placement> placements_;
    std::unordered_set<std::uint64_t> emptyGlyphs_;
    std::unordered_map<std::uint64_t, SlugGlyph> slugs_;

    [[nodiscard]] std::optional<Placement> glyphFor(TextTier tier, const PositionedGlyph &glyph,
                                                    float pixelSize, std::uint32_t phase);
    void refreshViews();
    [[nodiscard]] const SlugGlyph *slugFor(const PositionedGlyph &glyph);
};
}
