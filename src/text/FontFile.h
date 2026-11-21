#pragma once

#include "render/vector/VectorPath.h"
#include "text/OpenTypeLayout.h"

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace prism::text {
struct FontMetrics {
    std::uint16_t unitsPerEm;
    std::int16_t ascender;
    std::int16_t descender;
    std::int16_t lineGap;
};

struct VariationAxis {
    std::uint32_t tag;
    double minimum;
    double defaultValue;
    double maximum;
};

class FontFile {
  public:
    [[nodiscard]] static std::optional<FontFile> parse(std::vector<std::uint8_t> bytes);

    [[nodiscard]] const FontMetrics &metrics() const noexcept { return metrics_; }
    [[nodiscard]] std::uint16_t glyphCount() const noexcept { return glyphCount_; }
    [[nodiscard]] std::uint16_t glyphIndex(char32_t codepoint) const;
    [[nodiscard]] double advance(std::uint16_t glyph) const;
    [[nodiscard]] std::span<const VariationAxis> axes() const noexcept;
    bool setVariation(std::span<const std::pair<std::uint32_t, double>> settings);
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
        std::size_t gvar = 0;
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
    std::vector<VariationAxis> axes_;
    std::vector<std::vector<std::pair<double, double>>> segmentMaps_;
    std::vector<double> coords_;

    [[nodiscard]] bool load();
    [[nodiscard]] bool loadVariations(std::size_t fvar, std::size_t gvar, std::size_t avar);
    [[nodiscard]] bool varied() const noexcept;
    void glyphDeltas(std::uint16_t glyph, std::span<const double> x, std::span<const double> y,
                     std::span<const std::uint16_t> ends, std::vector<double> &dx,
                     std::vector<double> &dy) const;
    [[nodiscard]] std::size_t glyphOffset(std::uint16_t glyph, std::size_t &length) const;
    void appendOutline(std::uint16_t glyph, const double transform[6], render::VectorPath &path,
                       int depth) const;
};
}
