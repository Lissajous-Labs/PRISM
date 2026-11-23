#pragma once

#include "text/Bidi.h"
#include "text/FontFile.h"
#include "text/NativeFace.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace prism::text {
struct ShapeRequest {
    std::string_view text;
    float size = 16;
    float maxWidth = 0;
    std::span<const std::uint32_t> features = {};
    ParagraphDirection direction = ParagraphDirection::automatic;
};

struct PositionedGlyph {
    std::uint16_t glyph;
    std::uint8_t font;
    std::uint32_t cluster;
    float x;
    float y;
    float advance;
    float offsetX = 0;
    float offsetY = 0;
};

struct LineInfo {
    std::uint32_t firstGlyph;
    std::uint32_t glyphCount;
    float width;
    float baseline;
};

struct TextLayout {
    std::vector<PositionedGlyph> glyphs;
    std::vector<LineInfo> lines;
    float width = 0;
    float height = 0;
    float ascent = 0;
    float descent = 0;
    bool asciiFastPath = false;
};

class TextShaper {
  public:
    explicit TextShaper(std::vector<const FontFile *> fallbackChain,
                        std::size_t cacheCapacity = 512);

    [[nodiscard]] const TextLayout &shape(const ShapeRequest &request);
    [[nodiscard]] std::size_t cacheMisses() const noexcept { return misses_; }

  private:
    struct Run {
        std::uint32_t begin;
        std::uint32_t end;
        std::uint8_t level;
        std::uint8_t font;
    };

    std::vector<const FontFile *> fonts_;
    std::size_t capacity_;
    std::unordered_map<std::uint64_t, TextLayout> cache_;
    std::size_t misses_ = 0;
    std::vector<char32_t> codepoints_;
    BidiResult bidi_;

    [[nodiscard]] std::uint8_t fontFor(char32_t codepoint) const;
    void itemize(std::uint32_t begin, std::uint32_t end, std::vector<Run> &runs) const;
    void shapeRun(const Run &run, const ShapeRequest &request,
                  std::vector<PositionedGlyph> &glyphs) const;
    void layout(const ShapeRequest &request, TextLayout &result);
};
}
