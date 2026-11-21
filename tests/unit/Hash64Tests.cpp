#include "../support/AllocGuard.h"
#include "math/Hash64.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {
constexpr std::uint64_t unmix64(std::uint64_t value) noexcept {
    value ^= (value >> 31) ^ (value >> 62);
    value *= 0x319642B2D24D8EC3ULL;
    value ^= (value >> 27) ^ (value >> 54);
    value *= 0x96DE1B173F119089ULL;
    return value ^ (value >> 30) ^ (value >> 60);
}

constexpr std::uint32_t standardCrc(std::string_view text) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    std::size_t offset = 0;
    for (; offset + 8 <= text.size(); offset += 8) {
        std::uint64_t word = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            word |= static_cast<std::uint64_t>(static_cast<unsigned char>(text[offset + i]))
                    << (8 * i);
        }
        crc = prism::math::crc32c(crc, word);
    }
    return crc ^ 0xFFFFFFFFU;
}

constexpr std::string_view shortText = "spectral";
constexpr std::string_view longText = "prism hashes interned selectors and gradient stops";
constexpr auto compiledShort = prism::math::hash64(shortText, 7);
constexpr auto compiledLong = prism::math::hash64(longText, 7);
static_assert(standardCrc("12345678") == 0x6087809AU);
static_assert(unmix64(prism::math::mix64(0x0123456789ABCDEFULL)) == 0x0123456789ABCDEFULL);

std::uint64_t next(std::uint64_t &state) noexcept {
    state += 0x9E3779B97F4A7C15ULL;
    return prism::math::mix64(state);
}

bool avalanche(std::size_t length, int samples) {
    std::vector<std::uint8_t> message(length);
    std::vector<std::uint32_t> flips(length * 8 * 64);
    std::uint64_t random = length;
    for (int sample = 0; sample < samples; ++sample) {
        for (auto &byte : message) {
            byte = static_cast<std::uint8_t>(next(random));
        }
        const auto base = prism::math::hash64(std::span<const std::uint8_t>(message), sample);
        for (std::size_t bit = 0; bit < length * 8; ++bit) {
            message[bit / 8] ^= static_cast<std::uint8_t>(1U << (bit % 8));
            auto changed =
                base ^ prism::math::hash64(std::span<const std::uint8_t>(message), sample);
            message[bit / 8] ^= static_cast<std::uint8_t>(1U << (bit % 8));
            while (changed != 0) {
                ++flips[bit * 64 + static_cast<std::size_t>(std::countr_zero(changed))];
                changed &= changed - 1;
            }
        }
    }
    const auto worst = std::ranges::max(flips, {}, [samples](std::uint32_t count) {
        return std::abs(static_cast<double>(count) / samples - 0.5);
    });
    const double deviation = std::abs(static_cast<double>(worst) / samples - 0.5);
    std::cout << "Avalanche deviation for " << length << " bytes: " << deviation << '\n';
    return deviation < 1e-2;
}

bool check() {
    volatile std::uint64_t seed = 7;
    if (prism::math::hash64(shortText, seed) != compiledShort ||
        prism::math::hash64(longText, seed) != compiledLong ||
        standardCrc("12345678") != 0x6087809AU) {
        return false;
    }
    if (prism::math::hash64("a", 1) == prism::math::hash64("a", 2) ||
        prism::math::hash64(std::string_view("a\0", 2)) == prism::math::hash64("a")) {
        return false;
    }
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(1 << 20);
    for (std::uint64_t value = 0; value < (1U << 20); ++value) {
        const auto bytes = std::bit_cast<std::array<std::uint8_t, 8>>(value);
        if (!seen.insert(prism::math::hash64(std::span<const std::uint8_t>(bytes))).second) {
            return false;
        }
    }
    if (!avalanche(16, 60000) || !avalanche(40, 60000)) {
        return false;
    }
    prism::test::AllocGuard guard;
    const auto guarded = prism::math::hash64(longText, seed);
    return guarded == compiledLong && guard.counts().empty();
}
}

int main() {
    if (!check()) {
        std::cerr << "Hash contract failed\n";
        return 1;
    }
    return 0;
}
