#include "../support/TestFont.h"
#include "text/Bidi.h"
#include "text/BinaryReader.h"
#include "text/LineBreak.h"
#include "text/TextShaper.h"
#include "text/UtfFastPath.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
using namespace prism;
using text::tag;

bool checkUtf8() {
    const std::string longAscii(70, 'a');
    std::string mixed = longAscii + "\xC3\xA9";
    std::vector<char32_t> decoded;
    text::decodeUtf8("A\xF0\x9F\x8E\xB5Z", decoded);
    const bool emoji = decoded.size() == 3 && decoded[1] == 0x1F3B5;
    text::decodeUtf8("\xC0\x80", decoded);
    const bool overlong = decoded.size() == 2 && decoded[0] == text::replacementCharacter;
    text::decodeUtf8("\xED\xA0\x80", decoded);
    const bool surrogate = decoded.size() == 3 && decoded[0] == text::replacementCharacter;
    text::decodeUtf8("\xF4\x90\x80\x80", decoded);
    const bool tooLarge = decoded.size() == 4 && decoded[0] == text::replacementCharacter;
    text::decodeUtf8("\xE2\x82", decoded);
    const bool truncated = decoded.size() == 1 && decoded[0] == text::replacementCharacter;
    return text::asciiPrefix(mixed) == 70 && text::isAscii(longAscii) && !text::isAscii(mixed) &&
           emoji && overlong && surrogate && tooLarge && truncated;
}

bool checkBidi() {
    text::BidiResult result;
    const std::u32string mixed = U"ab \u05D0\u05D1\u05D2 12";
    text::resolveBidi(mixed, text::ParagraphDirection::automatic, result);
    const bool ltrParagraph = result.paragraphLevel == 0 && result.levels[3] == 1 &&
                              result.levels[0] == 0 && result.levels[7] == 2 &&
                              result.visualOrder[3] == 7 && result.visualOrder[8] == 3;
    const std::u32string hebrew = U"\u05D0\u05D1 ab";
    text::resolveBidi(hebrew, text::ParagraphDirection::automatic, result);
    const bool rtlParagraph =
        result.paragraphLevel == 1 && result.levels[3] == 2 && result.visualOrder.front() == 3;
    const std::u32string arabic = U"\u0627 \u0661\u0662";
    text::resolveBidi(arabic, text::ParagraphDirection::automatic, result);
    const bool arabicNumbers = result.levels[2] == 2 && result.levels[0] == 1;
    return ltrParagraph && rtlParagraph && arabicNumbers;
}

bool checkBreaks() {
    std::vector<text::BreakAction> breaks;
    const std::u32string sample = U"one two-three\nfour (x)";
    text::findBreaks(sample, breaks);
    return breaks[4] == text::BreakAction::allowed && breaks[3] == text::BreakAction::none &&
           breaks[8] == text::BreakAction::allowed && breaks[14] == text::BreakAction::mandatory &&
           breaks[19] == text::BreakAction::allowed && breaks[20] == text::BreakAction::none &&
           breaks.back() == text::BreakAction::mandatory;
}

bool checkShaping(const text::FontFile &font) {
    text::TextShaper shaper({&font});
    const auto &kerned = shaper.shape({"AV", 100});
    const auto &plain = shaper.shape({"AA", 100});
    const auto &ligature = shaper.shape({"fi", 100});
    const std::array<std::uint32_t, 1> tabular{tag("tnum")};
    const auto &proportional = shaper.shape({"1111", 100});
    const float proportionalWidth = proportional.width;
    const auto &eights = shaper.shape({"8888", 100});
    const float eightsWidth = eights.width;
    const auto &tabularOnes = shaper.shape({"1111", 100, 0, tabular});
    const float tabularOnesWidth = tabularOnes.width;
    const auto &tabularEights = shaper.shape({"8888", 100, 0, tabular});
    const bool kerning = std::abs(kerned.width - 112) < 1e-4 && std::abs(plain.width - 120) < 1e-4;
    const bool ligated =
        ligature.glyphs.size() == 1 && ligature.glyphs[0].glyph == test::glyphs::fi;
    const bool numerals = std::abs(proportionalWidth - eightsWidth) > 1 &&
                          std::abs(tabularOnesWidth - tabularEights.width) < 1e-4 &&
                          std::abs(tabularOnesWidth - 240) < 1e-4;
    const std::size_t missesBefore = shaper.cacheMisses();
    static_cast<void>(shaper.shape({"AV", 100}));
    const bool cached = shaper.cacheMisses() == missesBefore && kerned.asciiFastPath;
    const auto &wrapped = shaper.shape({"AAA AAA AAA", 100, 200});
    const bool wrapping = wrapped.lines.size() == 3 &&
                          std::abs(wrapped.lines[0].width - 180) < 1e-4 &&
                          wrapped.lines[1].baseline > wrapped.lines[0].baseline;
    const auto &rtl = shaper.shape({"A \xD7\x90\xD7\x90 V", 100});
    const bool reordered = !rtl.asciiFastPath && rtl.glyphs.size() == 6 &&
                           rtl.glyphs[2].glyph == test::glyphs::alef &&
                           rtl.glyphs[2].cluster == 3 && rtl.glyphs[3].cluster == 2;
    return kerning && ligated && numerals && cached && wrapping && reordered;
}

bool checkFont(const text::FontFile &font) {
    render::VectorPath outline;
    font.outline(test::glyphs::capitalV, 0.1, outline);
    return font.glyphCount() == test::glyphs::count && font.metrics().unitsPerEm == 1000 &&
           font.glyphIndex('A') == test::glyphs::capitalA &&
           font.glyphIndex('7') == test::glyphs::digitZero + 7 &&
           font.glyphIndex(0x05D0) == test::glyphs::alef && font.glyphIndex('Z') == 0 &&
           font.advance(test::glyphs::capitalA) == 600 && outline.verbs().size() == 5 &&
           font.layout().hasPairAdjustments() && !text::FontFile::parse({1, 2, 3, 4});
}

bool checkSystemFont() {
    std::ifstream file("/System/Library/Fonts/Supplemental/Arial.ttf", std::ios::binary);
    if (!file) {
        return true;
    }
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file),
                                    std::istreambuf_iterator<char>()};
    const auto font = text::FontFile::parse(std::move(bytes));
    if (!font) {
        return false;
    }
    render::VectorPath outline;
    font->outline(font->glyphIndex('g'), 0.05, outline);
    text::TextShaper shaper({&*font});
    const auto &layout = shaper.shape({"Prism 1234 \xC3\xA9", 16, 0, {}});
    std::cout << "System font glyphs " << font->glyphCount() << ", layout width " << layout.width
              << '\n';
    return font->glyphIndex('g') != 0 && !outline.verbs().empty() && layout.width > 50 &&
           layout.glyphs.size() == 12;
}
}

int main() {
    const auto font = text::FontFile::parse(test::buildTestFont());
    if (!font || !checkUtf8() || !checkBidi() || !checkBreaks() || !checkFont(*font) ||
        !checkShaping(*font) || !checkSystemFont()) {
        std::cerr << "Text shaping contract failed\n";
        return 1;
    }
    return 0;
}
