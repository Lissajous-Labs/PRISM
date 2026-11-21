#pragma once

#include "render/vector/VectorPath.h"
#include "text/OpenTypeLayout.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace prism::text {
struct FontMetrics {
    std::uint16_t unitsPerEm;
    std::int16_t ascender;
    std::int16_t descender;
    std::int16_t lineGap;
};

class FontFile {
  public:
    [[nodiscard]] static std::optional<FontFile> parse(std::vector<std::uint8_t> bytes);

    [[nodiscard]] const FontMetrics &metrics() const noexcept { return metrics_; }
    [[nodiscard]] std::uint16_t glyphCount() const noexcept { return glyphCount_; }
    [[nodiscard]] std::uint16_t glyphIndex(char32_t codepoint) const;
    [[nodiscard]] std::uint16_t advance(std::uint16_t glyph) const;
    void outline(std::uint16_t glyph, double scale, render::VectorPath &path) const;
    [[nodiscard]] const OpenTypeLayout &layout() const noexcept { return layout_; }

  private:
    struct Tables {
        std::size_t cmap = 0;
        std::size_t glyf = 0;
        std::size_t loca = 0;
        std::size_t hmtx = 0;
        std::size_t gsub = 0;
        std::size_t gpos = 0;
    };

    std::vector<std::uint8_t> bytes_;
    FontMetrics metrics_{1000, 800, -200, 0};
    Tables tables_;
    std::uint16_t glyphCount_ = 0;
    std::uint16_t metricCount_ = 0;
    bool longOffsets_ = false;
    std::size_t cmapSubtable_ = 0;
    std::uint16_t cmapFormat_ = 0;
    OpenTypeLayout layout_;

    [[nodiscard]] bool load();
    [[nodiscard]] std::size_t glyphOffset(std::uint16_t glyph, std::size_t &length) const;
    void appendOutline(std::uint16_t glyph, const double transform[6], render::VectorPath &path,
                       int depth) const;
};
}
