#pragma once

#include <cstdint>
#include <vector>

namespace prism::text {
struct GlyphBitmap {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t channels = 1;
    float left = 0;
    float top = 0;
    float scale = 1;
    std::vector<float> texels;
};
}
