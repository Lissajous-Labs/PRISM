#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace prism::text {
struct ShapedGlyph {
    std::uint16_t glyph;
    std::uint32_t cluster;
};

class OpenTypeLayout {
  public:
    void parse(std::span<const std::uint8_t> font, std::size_t gsubOffset, std::size_t gposOffset);
    void substitute(std::span<const std::uint32_t> features,
                    std::vector<ShapedGlyph> &glyphs) const;
    [[nodiscard]] std::int32_t pairAdjustment(std::uint16_t first, std::uint16_t second) const;
    [[nodiscard]] bool hasPairAdjustments() const noexcept { return !pairLookups_.empty(); }

  private:
    struct LookupRef {
        std::uint32_t feature;
        std::size_t subtable;
        std::uint16_t type;
    };
    std::span<const std::uint8_t> data_;
    std::vector<LookupRef> substitutions_;
    std::vector<std::size_t> pairLookups_;

    void collect(std::size_t table, bool positioning);
    [[nodiscard]] int coverageIndex(std::size_t coverage, std::uint16_t glyph) const;
    [[nodiscard]] std::uint16_t classOf(std::size_t classDef, std::uint16_t glyph) const;
    [[nodiscard]] bool applySingle(std::size_t subtable, ShapedGlyph &glyph) const;
    [[nodiscard]] bool applyLigature(std::size_t subtable, std::vector<ShapedGlyph> &glyphs,
                                     std::size_t index) const;
    [[nodiscard]] std::int32_t pairValue(std::size_t subtable, std::uint16_t first,
                                         std::uint16_t second) const;
};
}
