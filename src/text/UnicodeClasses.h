#pragma once

#include <cstdint>

namespace prism::text {
enum class BidiClass : std::uint8_t { L, R, AL, EN, ES, ET, AN, CS, NSM, BN, B, S, WS, ON };
enum class BreakClass : std::uint8_t {
    other,
    space,
    mandatory,
    hyphen,
    ideographic,
    openPunctuation,
    closePunctuation,
    combining
};

[[nodiscard]] BidiClass bidiClass(char32_t codepoint) noexcept;
[[nodiscard]] BreakClass breakClass(char32_t codepoint) noexcept;
}
