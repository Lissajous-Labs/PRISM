#pragma once

#include "dom/SmallVector.h"
#include "dom/StyleVocabulary.h"
#include "math/Hash64.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prism::dom {
enum class TokenKind : std::uint64_t {
    type = 0x7479706500,
    element = 0x6964656e74,
    klass = 0x636c617373
};

[[nodiscard]] constexpr std::uint64_t tokenHash(TokenKind kind, std::string_view text) noexcept {
    return math::hash64(text, static_cast<std::uint64_t>(kind));
}

struct Bloom256 {
    std::array<std::uint64_t, 4> words{};

    constexpr void add(std::uint64_t hash) noexcept {
        for (int r = 0; r < 6; ++r) {
            const auto bit = static_cast<std::uint32_t>((hash >> (8 * r)) & 255);
            words[bit >> 6] |= std::uint64_t{1} << (bit & 63);
        }
    }

    constexpr Bloom256 &operator|=(const Bloom256 &other) noexcept {
        for (std::size_t i = 0; i < 4; ++i) {
            words[i] |= other.words[i];
        }
        return *this;
    }

    [[nodiscard]] constexpr bool covers(const Bloom256 &mask) const noexcept {
        std::uint64_t missing = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            missing |= mask.words[i] & ~words[i];
        }
        return missing == 0;
    }
};

struct Specificity {
    std::uint64_t packed = 0;

    [[nodiscard]] static constexpr Specificity of(bool inlineStyle, std::uint32_t ids,
                                                  std::uint32_t classes, std::uint32_t types,
                                                  std::uint32_t order) noexcept {
        const auto clamp = [](std::uint32_t v, std::uint32_t bits) {
            return static_cast<std::uint64_t>(v < (1U << bits) ? v : (1U << bits) - 1);
        };
        return {(static_cast<std::uint64_t>(inlineStyle) << 63) | (clamp(ids, 10) << 53) |
                (clamp(classes, 12) << 41) | (clamp(types, 10) << 31) | clamp(order, 31)};
    }

    friend constexpr auto operator<=>(const Specificity &, const Specificity &) = default;
};

struct Length {
    float value = 0;
    Unit unit = Unit::none;
    std::uint8_t reserved[3]{};

    friend constexpr bool operator==(const Length &a, const Length &b) noexcept {
        return a.value == b.value && a.unit == b.unit;
    }
};

enum class ValueKind : std::uint8_t { length, number, color, keyword, token, transition };

struct StyleValue {
    ValueKind kind = ValueKind::number;
    Length length{};
    float number = 0;
    std::uint32_t color = 0;
    Keyword keyword = Keyword::none;
    std::uint32_t token = 0;
    Property transitionProperty = Property::count;
};

struct Declaration {
    Property property;
    StyleValue value;
};

enum class Combinator : std::uint8_t { none, descendant, child };
enum class PredicateOp : std::uint8_t { exists, attributeEquals, eq, ne, lt, gt, le, ge };

struct Predicate {
    PredicateOp op;
    std::uint32_t name;
    std::uint32_t text;
    double number;
};

struct Compound {
    std::uint64_t type = 0;
    std::uint64_t element = 0;
    SmallVector<std::uint64_t, 4> classes;
    std::uint8_t pseudo = 0;
    bool root = false;
    SmallVector<Predicate, 2> predicates;
    Combinator toLeft = Combinator::none;
};

struct MediaRange {
    float minWidth = 0;
    float maxWidth = std::numeric_limits<float>::infinity();
    float minHeight = 0;
    float maxHeight = std::numeric_limits<float>::infinity();

    [[nodiscard]] constexpr bool matches(float width, float height) const noexcept {
        return width >= minWidth && width <= maxWidth && height >= minHeight && height <= maxHeight;
    }
};

struct StyleRule {
    std::vector<Compound> compounds;
    Bloom256 ancestorMask;
    Specificity specificity;
    std::uint32_t firstDeclaration;
    std::uint32_t declarationCount;
    std::uint32_t media;
    std::uint32_t line;
};

struct StyleSheet {
    std::vector<StyleRule> rules;
    std::vector<Declaration> declarations;
    std::vector<MediaRange> media{MediaRange{}};
    std::vector<std::string> strings;
    std::vector<std::uint64_t> tokenNames;
    std::vector<StyleValue> tokenValues;

    [[nodiscard]] std::uint32_t intern(std::string_view text);
    [[nodiscard]] std::optional<std::uint32_t> token(std::string_view name) const;
    bool setToken(std::string_view name, const StyleValue &value);
};

struct StyleError {
    std::uint32_t line;
    std::string message;
};

struct CompileResult {
    std::optional<StyleSheet> sheet;
    StyleError error;
};

[[nodiscard]] ValueKind valueKindOf(Property property) noexcept;
[[nodiscard]] CompileResult compileStyleSheet(std::string_view source);
[[nodiscard]] bool parseDeclarations(std::string_view source, StyleSheet &sheet,
                                     std::vector<Declaration> &out);
[[nodiscard]] std::optional<StyleValue> parseValue(Property property, std::string_view text,
                                                   StyleSheet &sheet);
}
