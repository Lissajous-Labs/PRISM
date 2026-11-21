#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace prism::test {
class ByteWriter {
  public:
    std::vector<std::uint8_t> bytes;

    void u8(std::uint32_t v) { bytes.push_back(static_cast<std::uint8_t>(v)); }
    void u16(std::uint32_t v) {
        u8(v >> 8);
        u8(v);
    }
    void i16(int v) { u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(v))); }
    void u32(std::uint32_t v) {
        u16(v >> 16);
        u16(v & 0xFFFF);
    }
    void tag(const char *name) {
        for (int i = 0; i < 4; ++i) {
            u8(static_cast<unsigned char>(name[i]));
        }
    }
    void append(const std::vector<std::uint8_t> &other) {
        bytes.insert(bytes.end(), other.begin(), other.end());
    }
    void pad() {
        while (bytes.size() % 4 != 0) {
            u8(0);
        }
    }
    [[nodiscard]] std::size_t size() const noexcept { return bytes.size(); }
    void patch16(std::size_t at, std::uint32_t v) {
        bytes[at] = static_cast<std::uint8_t>(v >> 8);
        bytes[at + 1] = static_cast<std::uint8_t>(v);
    }
};

struct TestGlyph {
    std::uint16_t advance;
    std::vector<std::pair<int, int>> contour;
};

namespace glyphs {
inline constexpr std::uint16_t notdef = 0, space = 1, capitalA = 2, capitalV = 3, f = 4, i = 5,
                               fi = 6;
inline constexpr std::uint16_t digitZero = 7, tabularZero = 17, alef = 27, arabicAlef = 28,
                               count = 29;
}

inline std::vector<TestGlyph> testGlyphs() {
    const auto box = [](int x0, int y0, int x1, int y1) {
        return std::vector<std::pair<int, int>>{{x0, y0}, {x0, y1}, {x1, y1}, {x1, y0}};
    };
    std::vector<TestGlyph> list(glyphs::count);
    list[glyphs::notdef] = {500, box(50, 0, 450, 700)};
    list[glyphs::space] = {250, {}};
    list[glyphs::capitalA] = {600, box(100, 0, 500, 700)};
    list[glyphs::capitalV] = {600, {{50, 700}, {550, 700}, {300, 0}}};
    list[glyphs::f] = {300, box(60, 0, 240, 720)};
    list[glyphs::i] = {250, box(80, 0, 170, 520)};
    list[glyphs::fi] = {520, box(60, 0, 460, 720)};
    for (int d = 0; d < 10; ++d) {
        list[glyphs::digitZero + d] = {static_cast<std::uint16_t>(400 + 25 * d),
                                       box(40, 0, 360 + 25 * d, 700)};
        list[glyphs::tabularZero + d] = {600, box(60, 0, 540, 700)};
    }
    list[glyphs::alef] = {550, box(50, 0, 500, 650)};
    list[glyphs::arabicAlef] = {300, box(110, 0, 190, 720)};
    return list;
}

inline std::vector<std::uint8_t> glyfData(const TestGlyph &glyph) {
    ByteWriter w;
    if (glyph.contour.empty()) {
        return {};
    }
    int x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
    for (const auto &[x, y] : glyph.contour) {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
    }
    w.i16(1);
    w.i16(x0);
    w.i16(y0);
    w.i16(x1);
    w.i16(y1);
    w.u16(static_cast<std::uint32_t>(glyph.contour.size() - 1));
    w.u16(0);
    for (std::size_t p = 0; p < glyph.contour.size(); ++p) {
        w.u8(0x01);
    }
    int previous = 0;
    for (const auto &point : glyph.contour) {
        w.i16(point.first - previous);
        previous = point.first;
    }
    previous = 0;
    for (const auto &point : glyph.contour) {
        w.i16(point.second - previous);
        previous = point.second;
    }
    w.pad();
    return w.bytes;
}

inline std::vector<std::uint8_t> cmapTable(const std::map<std::uint32_t, std::uint16_t> &mapping) {
    struct Segment {
        std::uint32_t start;
        std::uint32_t end;
        int delta;
    };
    std::vector<Segment> segments;
    for (const auto &[code, glyph] : mapping) {
        const int delta = static_cast<int>(glyph) - static_cast<int>(code);
        if (!segments.empty() && segments.back().end + 1 == code &&
            segments.back().delta == delta) {
            segments.back().end = code;
        } else {
            segments.push_back({code, code, delta});
        }
    }
    segments.push_back({0xFFFF, 0xFFFF, 1});
    ByteWriter w;
    w.u16(0);
    w.u16(1);
    w.u16(3);
    w.u16(1);
    w.u32(12);
    const auto count = static_cast<std::uint32_t>(segments.size());
    w.u16(4);
    w.u16(16 + 8 * count);
    w.u16(0);
    w.u16(2 * count);
    w.u16(0);
    w.u16(0);
    w.u16(0);
    for (const auto &s : segments) {
        w.u16(s.end);
    }
    w.u16(0);
    for (const auto &s : segments) {
        w.u16(s.start);
    }
    for (const auto &s : segments) {
        w.u16(static_cast<std::uint16_t>(s.delta));
    }
    for (std::uint32_t s = 0; s < count; ++s) {
        w.u16(0);
    }
    return w.bytes;
}

inline void coverageFormat1(ByteWriter &w, const std::vector<std::uint16_t> &glyphList) {
    w.u16(1);
    w.u16(static_cast<std::uint32_t>(glyphList.size()));
    for (const auto glyph : glyphList) {
        w.u16(glyph);
    }
}

inline std::vector<std::uint8_t> gsubTable() {
    ByteWriter w;
    w.u32(0x00010000);
    w.u16(10);
    w.u16(32);
    w.u16(58);
    w.u16(1);
    w.tag("DFLT");
    w.u16(8);
    w.u16(4);
    w.u16(0);
    w.u16(0);
    w.u16(0xFFFF);
    w.u16(2);
    w.u16(0);
    w.u16(1);
    w.u16(2);
    w.tag("liga");
    w.u16(14);
    w.tag("tnum");
    w.u16(20);
    w.u16(0);
    w.u16(1);
    w.u16(0);
    w.u16(0);
    w.u16(1);
    w.u16(1);
    w.u16(2);
    w.u16(6);
    w.u16(38);
    w.u16(4);
    w.u16(0);
    w.u16(1);
    w.u16(8);
    w.u16(1);
    w.u16(8);
    w.u16(1);
    w.u16(14);
    coverageFormat1(w, {glyphs::f});
    w.u16(1);
    w.u16(4);
    w.u16(glyphs::fi);
    w.u16(2);
    w.u16(glyphs::i);
    w.u16(1);
    w.u16(0);
    w.u16(1);
    w.u16(8);
    w.u16(2);
    w.u16(26);
    w.u16(10);
    for (int d = 0; d < 10; ++d) {
        w.u16(glyphs::tabularZero + d);
    }
    std::vector<std::uint16_t> digits;
    for (int d = 0; d < 10; ++d) {
        digits.push_back(static_cast<std::uint16_t>(glyphs::digitZero + d));
    }
    coverageFormat1(w, digits);
    return w.bytes;
}

inline std::vector<std::uint8_t> gposTable() {
    ByteWriter w;
    w.u32(0x00010000);
    w.u16(10);
    w.u16(30);
    w.u16(44);
    w.u16(1);
    w.tag("DFLT");
    w.u16(8);
    w.u16(4);
    w.u16(0);
    w.u16(0);
    w.u16(0xFFFF);
    w.u16(1);
    w.u16(0);
    w.u16(1);
    w.tag("kern");
    w.u16(8);
    w.u16(0);
    w.u16(1);
    w.u16(0);
    w.u16(1);
    w.u16(4);
    w.u16(2);
    w.u16(0);
    w.u16(1);
    w.u16(8);
    w.u16(1);
    w.u16(12);
    w.u16(4);
    w.u16(0);
    w.u16(1);
    w.u16(18);
    coverageFormat1(w, {glyphs::capitalA});
    w.u16(1);
    w.u16(glyphs::capitalV);
    w.i16(-80);
    return w.bytes;
}

inline std::vector<std::uint8_t> buildTestFont() {
    const auto list = testGlyphs();
    std::map<std::uint32_t, std::uint16_t> mapping{{' ', glyphs::space},
                                                   {'A', glyphs::capitalA},
                                                   {'V', glyphs::capitalV},
                                                   {'f', glyphs::f},
                                                   {'i', glyphs::i},
                                                   {0x05D0, glyphs::alef},
                                                   {0x0627, glyphs::arabicAlef}};
    for (int d = 0; d < 10; ++d) {
        mapping['0' + static_cast<std::uint32_t>(d)] =
            static_cast<std::uint16_t>(glyphs::digitZero + d);
    }
    ByteWriter glyf;
    ByteWriter loca;
    for (const auto &glyph : list) {
        loca.u32(static_cast<std::uint32_t>(glyf.size()));
        glyf.append(glyfData(glyph));
    }
    loca.u32(static_cast<std::uint32_t>(glyf.size()));
    ByteWriter head;
    head.u32(0x00010000);
    head.u32(0x00010000);
    head.u32(0);
    head.u32(0x5F0F3CF5);
    head.u16(0);
    head.u16(1000);
    for (int k = 0; k < 4; ++k) {
        head.u32(0);
    }
    for (int k = 0; k < 4; ++k) {
        head.i16(0);
    }
    head.u16(0);
    head.u16(8);
    head.i16(2);
    head.i16(1);
    head.i16(0);
    ByteWriter hhea;
    hhea.u32(0x00010000);
    hhea.i16(800);
    hhea.i16(-200);
    hhea.i16(100);
    for (int k = 0; k < 12; ++k) {
        hhea.i16(0);
    }
    hhea.u16(glyphs::count);
    ByteWriter maxp;
    maxp.u32(0x00005000);
    maxp.u16(glyphs::count);
    ByteWriter hmtx;
    for (const auto &glyph : list) {
        hmtx.u16(glyph.advance);
        hmtx.i16(0);
    }
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>> tables{
        {"GPOS", gposTable()}, {"GSUB", gsubTable()}, {"cmap", cmapTable(mapping)},
        {"glyf", glyf.bytes},  {"head", head.bytes},  {"hhea", hhea.bytes},
        {"hmtx", hmtx.bytes},  {"loca", loca.bytes},  {"maxp", maxp.bytes}};
    ByteWriter font;
    font.u32(0x00010000);
    font.u16(static_cast<std::uint32_t>(tables.size()));
    font.u16(0);
    font.u16(0);
    font.u16(0);
    std::size_t offset = 12 + 16 * tables.size();
    for (const auto &[name, data] : tables) {
        font.tag(name.c_str());
        font.u32(0);
        font.u32(static_cast<std::uint32_t>(offset));
        font.u32(static_cast<std::uint32_t>(data.size()));
        offset += (data.size() + 3) / 4 * 4;
    }
    for (const auto &table : tables) {
        font.append(table.second);
        font.pad();
    }
    return font.bytes;
}
}
