#pragma once

#include "text/GlyphBitmap.h"

#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace prism::text {
class FontFile;

struct NativeGlyph {
    std::uint16_t glyph;
    std::uint32_t cluster;
    float advance;
    float offsetX;
    float offsetY;
};

class NativeFace {
  public:
    [[nodiscard]] static std::shared_ptr<NativeFace> create(const FontFile &font);
    ~NativeFace();

    NativeFace(const NativeFace &) = delete;
    NativeFace &operator=(const NativeFace &) = delete;

    void shape(std::span<const char32_t> codepoints, std::uint32_t firstCluster, bool rightToLeft,
               std::span<const std::uint32_t> features, float size,
               std::vector<NativeGlyph> &out) const;
    bool rasterize(std::uint16_t glyph, float pixelSize, std::uint32_t phase, bool lcd,
                   GlyphBitmap &bitmap) const;

  private:
    NativeFace() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] bool nativeTextAvailable() noexcept;
}
