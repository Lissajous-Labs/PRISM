#pragma once

#include "math/SimdVec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <vector>

namespace prism::text {
inline constexpr char32_t replacementCharacter = 0xFFFD;

[[nodiscard]] inline std::size_t asciiPrefix(std::string_view text) noexcept {
    using Lane = math::SimdVec<std::uint8_t, 16>;
    std::size_t offset = 0;
    const Lane high(static_cast<std::uint8_t>(0x80));
    for (; offset + 16 <= text.size(); offset += 16) {
        std::array<std::uint8_t, 16> bytes;
        std::memcpy(bytes.data(), text.data() + offset, 16);
        const Lane masked = Lane(bytes) & high;
        if (!masked.isZero()) {
            const std::uint32_t mask = math::bmask(masked);
            return offset + static_cast<std::size_t>(__builtin_ctz(mask));
        }
    }
    while (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0x80u) == 0) {
        ++offset;
    }
    return offset;
}

[[nodiscard]] inline bool isAscii(std::string_view text) noexcept {
    return asciiPrefix(text) == text.size();
}

void decodeUtf8(std::string_view text, std::vector<char32_t> &output);
}
