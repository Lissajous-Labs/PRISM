#pragma once

#include "math/Hash64.h"

#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>

namespace prism::dom {
inline constexpr std::size_t maxFoldedKey = 64;

[[nodiscard]] constexpr char asciiLower(char c) noexcept {
    const auto byte = static_cast<std::uint8_t>(c);
    return static_cast<char>(
        byte ^ (static_cast<std::uint8_t>(static_cast<std::uint8_t>(byte - 'A') <= 25) << 5));
}

[[nodiscard]] constexpr std::uint64_t foldedHash(std::string_view key,
                                                 std::uint64_t seed) noexcept {
    std::array<char, maxFoldedKey> folded{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        folded[i] = asciiLower(key[i]);
    }
    return math::hash64(std::span<const char>(folded.data(), key.size()), seed);
}

[[nodiscard]] constexpr bool foldedEqual(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (asciiLower(a[i]) != asciiLower(b[i])) {
            return false;
        }
    }
    return true;
}

template <std::size_t N> struct PerfectHashTable {
    static constexpr std::size_t buckets = std::bit_ceil((N * 5 + 3) / 4);
    static constexpr std::uint32_t tagged = 1U << 31;
    static constexpr std::uint32_t emptyBucket = tagged - 1;
    static constexpr std::uint16_t emptySlot = 0xFFFF;

    std::uint64_t seed = 0;
    std::array<std::uint32_t, buckets> displacement{};
    std::array<std::uint16_t, buckets> slots{};
    std::array<std::string_view, N> keys{};

    [[nodiscard]] constexpr std::optional<std::uint16_t> find(std::string_view key) const noexcept {
        if (key.size() > maxFoldedKey) {
            return std::nullopt;
        }
        const std::uint32_t entry = displacement[foldedHash(key, seed) & (buckets - 1)];
        std::uint32_t index = entry;
        if ((entry & tagged) != 0) {
            index = slots[foldedHash(key, entry & ~tagged) & (buckets - 1)];
        }
        if (index >= N || !foldedEqual(keys[index], key)) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(index);
    }
};

template <std::size_t N>
consteval PerfectHashTable<N> buildPerfectHash(const std::array<std::string_view, N> &keys,
                                               std::uint64_t seed = 0x5052495330ULL) {
    using Table = PerfectHashTable<N>;
    constexpr std::size_t m = Table::buckets;
    Table table;
    table.seed = seed;
    table.keys = keys;
    table.displacement.fill(Table::emptyBucket);
    table.slots.fill(Table::emptySlot);
    std::array<std::size_t, N> bucketOf{};
    std::array<std::size_t, m> sizes{};
    for (std::size_t i = 0; i < N; ++i) {
        if (keys[i].size() > maxFoldedKey) {
            throw std::logic_error("key too long");
        }
        bucketOf[i] = foldedHash(keys[i], seed) & (m - 1);
        ++sizes[bucketOf[i]];
    }
    std::array<std::size_t, m> order{};
    for (std::size_t j = 0; j < m; ++j) {
        order[j] = j;
    }
    for (std::size_t i = 1; i < m; ++i) {
        for (std::size_t j = i; j > 0 && sizes[order[j]] > sizes[order[j - 1]]; --j) {
            const std::size_t swap = order[j];
            order[j] = order[j - 1];
            order[j - 1] = swap;
        }
    }
    for (const std::size_t bucket : order) {
        if (sizes[bucket] == 0) {
            break;
        }
        std::array<std::size_t, N> members{};
        std::size_t count = 0;
        for (std::size_t i = 0; i < N; ++i) {
            if (bucketOf[i] == bucket) {
                members[count++] = i;
            }
        }
        if (count == 1) {
            table.displacement[bucket] = static_cast<std::uint32_t>(members[0]);
            continue;
        }
        bool placed = false;
        for (std::uint32_t d = 1; d < (1U << 20) && !placed; ++d) {
            std::array<std::size_t, N> targets{};
            placed = true;
            for (std::size_t k = 0; k < count && placed; ++k) {
                targets[k] = foldedHash(keys[members[k]], d) & (m - 1);
                placed = table.slots[targets[k]] == Table::emptySlot;
                for (std::size_t q = 0; q < k && placed; ++q) {
                    placed = targets[q] != targets[k];
                }
            }
            if (placed) {
                for (std::size_t k = 0; k < count; ++k) {
                    table.slots[targets[k]] = static_cast<std::uint16_t>(members[k]);
                }
                table.displacement[bucket] = Table::tagged | d;
            }
        }
        if (!placed) {
            throw std::logic_error("no displacement");
        }
    }
    return table;
}
}
