#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace prism::text {
enum class ParagraphDirection : std::uint8_t { automatic, leftToRight, rightToLeft };

struct BidiResult {
    std::uint8_t paragraphLevel = 0;
    std::vector<std::uint8_t> levels;
    std::vector<std::uint32_t> visualOrder;
};

void resolveBidi(std::span<const char32_t> text, ParagraphDirection direction, BidiResult &result);
}
