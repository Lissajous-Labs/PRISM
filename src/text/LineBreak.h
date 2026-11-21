#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace prism::text {
enum class BreakAction : std::uint8_t { none, allowed, mandatory };

void findBreaks(std::span<const char32_t> text, std::vector<BreakAction> &breaks);
}
