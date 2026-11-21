#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace prism::math {
template <std::size_t Capacity>
    requires(Capacity > 0 && Capacity <= (std::size_t{1} << 24))
class Bitset64 {
  public:
    static constexpr std::size_t capacity = Capacity;

    constexpr void set(std::size_t index) noexcept {
        for (std::size_t level = 0; level < levelCount; ++level) {
            word(level, index >> (6 * (level + 1))) |= bit(index >> (6 * level));
        }
    }

    constexpr void reset(std::size_t index) noexcept {
        for (std::size_t level = 0; level < levelCount; ++level) {
            auto &current = word(level, index >> (6 * (level + 1)));
            current &= ~bit(index >> (6 * level));
            if (current != 0) {
                return;
            }
        }
    }

    [[nodiscard]] constexpr bool test(std::size_t index) const noexcept {
        return (word(0, index >> 6) & bit(index)) != 0;
    }

    [[nodiscard]] constexpr bool any() const noexcept { return word(levelCount - 1, 0) != 0; }

    template <class Visitor> constexpr void forEach(Visitor &&visitor) const {
        visit(levelCount - 1, 0, visitor);
    }

    constexpr void clear() noexcept { clearWord(levelCount - 1, 0); }

  private:
    static constexpr std::size_t countWords(std::size_t bits) noexcept { return (bits + 63) / 64; }

    static constexpr std::size_t levelCount = [] {
        std::size_t levels = 1;
        for (std::size_t words = countWords(Capacity); words > 1; words = countWords(words)) {
            ++levels;
        }
        return levels;
    }();

    static constexpr auto offsets = [] {
        std::array<std::size_t, levelCount + 1> result{};
        std::size_t words = countWords(Capacity);
        for (std::size_t level = 0; level < levelCount; ++level) {
            result[level + 1] = result[level] + words;
            words = countWords(words);
        }
        return result;
    }();

    std::array<std::uint64_t, offsets[levelCount]> words_{};

    static constexpr std::uint64_t bit(std::size_t index) noexcept {
        return std::uint64_t{1} << (index & 63);
    }

    constexpr std::uint64_t &word(std::size_t level, std::size_t index) noexcept {
        return words_[offsets[level] + index];
    }

    constexpr const std::uint64_t &word(std::size_t level, std::size_t index) const noexcept {
        return words_[offsets[level] + index];
    }

    template <class Visitor>
    constexpr void visit(std::size_t level, std::size_t index, Visitor &visitor) const {
        for (std::uint64_t bits = word(level, index); bits != 0; bits &= bits - 1) {
            const std::size_t child =
                (index << 6) | static_cast<std::size_t>(std::countr_zero(bits));
            if (level == 0) {
                visitor(child);
            } else {
                visit(level - 1, child, visitor);
            }
        }
    }

    constexpr void clearWord(std::size_t level, std::size_t index) noexcept {
        auto &current = word(level, index);
        if (level != 0) {
            for (std::uint64_t bits = current; bits != 0; bits &= bits - 1) {
                clearWord(level - 1,
                          (index << 6) | static_cast<std::size_t>(std::countr_zero(bits)));
            }
        }
        current = 0;
    }
};
}
