#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#if defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#elif defined(__SSE4_2__)
#include <nmmintrin.h>
#endif

namespace prism::math {
namespace detail {
inline constexpr std::uint64_t hashPrime1 = 0x9E3779B185EBCA87ULL;
inline constexpr std::uint64_t hashPrime2 = 0xC2B2AE3D27D4EB4FULL;

inline constexpr auto crcTable = [] {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t value = i;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value >> 1) ^ ((value & 1U) * 0x82F63B78U);
        }
        table[i] = value;
    }
    return table;
}();

[[nodiscard]] constexpr std::uint64_t hashWord(std::uint64_t state, std::uint64_t word) noexcept {
    return std::rotl(state ^ (word * hashPrime1), 27) * hashPrime2;
}

[[nodiscard]] constexpr std::uint64_t laneRound(std::uint64_t lane, std::uint64_t word) noexcept {
    return std::rotl(lane + word * hashPrime2, 31) * hashPrime1;
}

template <class Byte>
[[nodiscard]] constexpr std::uint64_t loadWord(std::span<const Byte> bytes,
                                               std::size_t offset) noexcept {
    std::uint64_t word = 0;
    const std::size_t count = bytes.size() - offset < 8 ? bytes.size() - offset : 8;
    for (std::size_t i = 0; i < count; ++i) {
        word |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(bytes[offset + i])) << (8 * i);
    }
    return word;
}
}

[[nodiscard]] constexpr std::uint32_t crc32c(std::uint32_t crc, std::uint64_t word) noexcept {
    if !consteval {
#if defined(__ARM_FEATURE_CRC32)
        return __crc32cd(crc, word);
#elif defined(__SSE4_2__)
        return static_cast<std::uint32_t>(_mm_crc32_u64(crc, word));
#endif
    }
    for (int i = 0; i < 8; ++i) {
        crc = (crc >> 8) ^
              detail::crcTable[(crc ^ static_cast<std::uint32_t>(word >> (8 * i))) & 0xFFU];
    }
    return crc;
}

[[nodiscard]] constexpr std::uint64_t mix64(std::uint64_t value) noexcept {
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

template <class Byte>
    requires(sizeof(Byte) == 1)
[[nodiscard]] constexpr std::uint64_t hash64(std::span<const Byte> bytes,
                                             std::uint64_t seed = 0) noexcept {
    std::uint32_t crc = static_cast<std::uint32_t>(seed) ^ static_cast<std::uint32_t>(seed >> 32);
    std::uint64_t state = seed ^ detail::hashPrime2;
    std::size_t offset = 0;
    if (bytes.size() >= 32) {
        std::array<std::uint64_t, 4> lanes{seed + detail::hashPrime1 + detail::hashPrime2,
                                           seed + detail::hashPrime2, seed,
                                           seed - detail::hashPrime1};
        for (; bytes.size() - offset >= 32; offset += 32) {
            for (std::size_t lane = 0; lane < 4; ++lane) {
                lanes[lane] =
                    detail::laneRound(lanes[lane], detail::loadWord(bytes, offset + 8 * lane));
            }
        }
        for (const std::uint64_t lane : lanes) {
            crc = crc32c(crc, lane);
            state = detail::hashWord(state, lane);
        }
    }
    for (; offset < bytes.size(); offset += 8) {
        const std::uint64_t word = detail::loadWord(bytes, offset);
        crc = crc32c(crc, word);
        state = detail::hashWord(state, word);
    }
    const std::uint64_t wideCrc = crc;
    return mix64(state ^ (wideCrc << 32) ^ wideCrc ^ static_cast<std::uint64_t>(bytes.size()));
}

[[nodiscard]] constexpr std::uint64_t hash64(std::string_view text,
                                             std::uint64_t seed = 0) noexcept {
    return hash64(std::span<const char>(text.data(), text.size()), seed);
}
}
