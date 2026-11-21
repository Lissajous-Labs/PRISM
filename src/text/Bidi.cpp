#include "text/Bidi.h"

#include "text/UnicodeClasses.h"

#include <algorithm>

namespace prism::text {
namespace {
bool strongRight(BidiClass value) noexcept {
    return value == BidiClass::R || value == BidiClass::AL;
}

bool neutral(BidiClass value) noexcept {
    return value == BidiClass::WS || value == BidiClass::ON || value == BidiClass::S ||
           value == BidiClass::B || value == BidiClass::BN || value == BidiClass::ES ||
           value == BidiClass::ET || value == BidiClass::CS;
}

std::uint8_t paragraphLevel(std::span<const BidiClass> classes) noexcept {
    for (const BidiClass value : classes) {
        if (value == BidiClass::L) {
            return 0;
        }
        if (strongRight(value)) {
            return 1;
        }
    }
    return 0;
}

void resolveWeak(std::vector<BidiClass> &types, BidiClass sor) {
    BidiClass previousStrong = sor;
    BidiClass previous = sor;
    for (BidiClass &type : types) {
        if (type == BidiClass::NSM) {
            type = previous;
        }
        if (type == BidiClass::EN && previousStrong == BidiClass::AL) {
            type = BidiClass::AN;
        }
        if (type == BidiClass::L || type == BidiClass::R || type == BidiClass::AL) {
            previousStrong = type;
        }
        previous = type;
    }
    for (BidiClass &type : types) {
        if (type == BidiClass::AL) {
            type = BidiClass::R;
        }
    }
    for (std::size_t i = 1; i + 1 < types.size(); ++i) {
        const BidiClass before = types[i - 1];
        const BidiClass after = types[i + 1];
        if (types[i] == BidiClass::ES && before == BidiClass::EN && after == BidiClass::EN) {
            types[i] = BidiClass::EN;
        } else if (types[i] == BidiClass::CS && before == after &&
                   (before == BidiClass::EN || before == BidiClass::AN)) {
            types[i] = before;
        }
    }
    for (std::size_t i = 0; i < types.size(); ++i) {
        if (types[i] != BidiClass::ET) {
            continue;
        }
        std::size_t end = i;
        while (end < types.size() && types[end] == BidiClass::ET) {
            ++end;
        }
        const bool touchesNumber = (i > 0 && types[i - 1] == BidiClass::EN) ||
                                   (end < types.size() && types[end] == BidiClass::EN);
        for (std::size_t k = i; k < end; ++k) {
            types[k] = touchesNumber ? BidiClass::EN : BidiClass::ON;
        }
        i = end - 1;
    }
    for (BidiClass &type : types) {
        if (type == BidiClass::ES || type == BidiClass::CS || type == BidiClass::ET) {
            type = BidiClass::ON;
        }
    }
    previousStrong = sor;
    for (BidiClass &type : types) {
        if (type == BidiClass::EN && previousStrong == BidiClass::L) {
            type = BidiClass::L;
        }
        if (type == BidiClass::L || type == BidiClass::R) {
            previousStrong = type;
        }
    }
}

BidiClass strongDirection(BidiClass type) noexcept {
    return type == BidiClass::EN || type == BidiClass::AN ? BidiClass::R : type;
}

void resolveNeutrals(std::vector<BidiClass> &types, BidiClass sor, BidiClass eor) {
    for (std::size_t i = 0; i < types.size(); ++i) {
        if (!neutral(types[i])) {
            continue;
        }
        std::size_t end = i;
        while (end < types.size() && neutral(types[end])) {
            ++end;
        }
        const BidiClass before = i == 0 ? sor : strongDirection(types[i - 1]);
        const BidiClass after = end == types.size() ? eor : strongDirection(types[end]);
        const BidiClass resolved = before == after ? before : BidiClass::ON;
        for (std::size_t k = i; k < end; ++k) {
            types[k] = resolved;
        }
        i = end - 1;
    }
}
}

void resolveBidi(std::span<const char32_t> text, ParagraphDirection direction, BidiResult &result) {
    std::vector<BidiClass> types(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        types[i] = bidiClass(text[i]);
    }
    result.paragraphLevel = direction == ParagraphDirection::automatic     ? paragraphLevel(types)
                            : direction == ParagraphDirection::rightToLeft ? 1
                                                                           : 0;
    const BidiClass embedding = result.paragraphLevel % 2 == 0 ? BidiClass::L : BidiClass::R;
    const std::vector<BidiClass> original = types;
    resolveWeak(types, embedding);
    resolveNeutrals(types, embedding, embedding);
    result.levels.assign(text.size(), result.paragraphLevel);
    for (std::size_t i = 0; i < text.size(); ++i) {
        BidiClass type = types[i] == BidiClass::ON ? embedding : types[i];
        std::uint8_t level = result.paragraphLevel;
        if (level % 2 == 0) {
            level +=
                type == BidiClass::R ? 1 : (type == BidiClass::AN || type == BidiClass::EN ? 2 : 0);
        } else {
            level += type == BidiClass::L || type == BidiClass::EN || type == BidiClass::AN ? 1 : 0;
        }
        if (original[i] == BidiClass::S || original[i] == BidiClass::B) {
            level = result.paragraphLevel;
        }
        result.levels[i] = level;
    }
    for (std::size_t i = text.size(); i-- > 0;) {
        if (original[i] != BidiClass::WS && original[i] != BidiClass::S) {
            break;
        }
        result.levels[i] = result.paragraphLevel;
    }
    result.visualOrder.resize(text.size());
    for (std::uint32_t i = 0; i < text.size(); ++i) {
        result.visualOrder[i] = i;
    }
    if (text.empty()) {
        return;
    }
    const std::uint8_t highest = *std::max_element(result.levels.begin(), result.levels.end());
    std::uint8_t lowestOdd = 255;
    for (const std::uint8_t level : result.levels) {
        if (level % 2 == 1) {
            lowestOdd = std::min(lowestOdd, level);
        }
    }
    for (int level = highest; level >= lowestOdd && lowestOdd != 255; --level) {
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (result.levels[result.visualOrder[i]] < level) {
                continue;
            }
            std::size_t end = i;
            while (end < text.size() && result.levels[result.visualOrder[end]] >= level) {
                ++end;
            }
            std::reverse(result.visualOrder.begin() + static_cast<std::ptrdiff_t>(i),
                         result.visualOrder.begin() + static_cast<std::ptrdiff_t>(end));
            i = end;
        }
    }
}
}
