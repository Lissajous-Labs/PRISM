#include "text/FontFile.h"

#include "text/BinaryReader.h"

#include <array>

namespace prism::text {
std::optional<FontFile> FontFile::parse(std::vector<std::uint8_t> bytes) {
    FontFile font;
    font.bytes_ = std::move(bytes);
    if (!font.load()) {
        return std::nullopt;
    }
    return font;
}

bool FontFile::load() {
    BinaryReader reader(bytes_);
    const std::uint32_t version = reader.u32();
    if (version != 0x00010000u && version != tag("true")) {
        return false;
    }
    const std::uint16_t tableCount = reader.u16();
    reader.skip(6);
    std::size_t head = 0;
    std::size_t hhea = 0;
    std::size_t maxp = 0;
    for (std::uint16_t i = 0; i < tableCount && reader.ok(); ++i) {
        const std::uint32_t name = reader.u32();
        reader.skip(4);
        const std::size_t offset = reader.u32();
        reader.skip(4);
        if (name == tag("head"))
            head = offset;
        if (name == tag("hhea"))
            hhea = offset;
        if (name == tag("maxp"))
            maxp = offset;
        if (name == tag("cmap"))
            tables_.cmap = offset;
        if (name == tag("glyf"))
            tables_.glyf = offset;
        if (name == tag("loca"))
            tables_.loca = offset;
        if (name == tag("hmtx"))
            tables_.hmtx = offset;
        if (name == tag("GSUB"))
            tables_.gsub = offset;
        if (name == tag("GPOS"))
            tables_.gpos = offset;
    }
    if (!reader.ok() || !head || !hhea || !maxp || !tables_.cmap || !tables_.glyf ||
        !tables_.loca || !tables_.hmtx) {
        return false;
    }
    metrics_.unitsPerEm = reader.u16At(head + 18);
    longOffsets_ = reader.u16At(head + 50) != 0;
    reader.seek(hhea + 4);
    metrics_.ascender = reader.i16();
    metrics_.descender = reader.i16();
    metrics_.lineGap = reader.i16();
    metricCount_ = reader.u16At(hhea + 34);
    glyphCount_ = reader.u16At(maxp + 4);
    const std::uint16_t encodings = reader.u16At(tables_.cmap + 2);
    for (std::uint16_t i = 0; i < encodings && reader.ok(); ++i) {
        reader.seek(tables_.cmap + 4 + 8 * static_cast<std::size_t>(i));
        const std::uint16_t platform = reader.u16();
        const std::uint16_t encoding = reader.u16();
        const std::size_t subtable = tables_.cmap + reader.u32();
        const std::uint16_t format = reader.u16At(subtable);
        const bool unicode = platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10));
        if (unicode && (format == 4 || format == 12) && (cmapFormat_ != 12)) {
            cmapSubtable_ = subtable;
            cmapFormat_ = format;
        }
    }
    layout_.parse(bytes_, tables_.gsub, tables_.gpos);
    return reader.ok() && cmapFormat_ != 0 && metrics_.unitsPerEm != 0;
}

std::uint16_t FontFile::glyphIndex(char32_t codepoint) const {
    BinaryReader reader(bytes_, cmapSubtable_);
    if (cmapFormat_ == 12) {
        reader.seek(cmapSubtable_ + 12);
        const std::uint32_t groups = reader.u32();
        for (std::uint32_t g = 0; g < groups && reader.ok(); ++g) {
            const std::uint32_t start = reader.u32();
            const std::uint32_t end = reader.u32();
            const std::uint32_t glyph = reader.u32();
            if (codepoint >= start && codepoint <= end) {
                return static_cast<std::uint16_t>(glyph + (codepoint - start));
            }
        }
        return 0;
    }
    if (codepoint > 0xFFFF) {
        return 0;
    }
    const std::uint16_t segments = reader.u16At(cmapSubtable_ + 6) / 2;
    const std::size_t ends = cmapSubtable_ + 14;
    const std::size_t starts = ends + 2 * static_cast<std::size_t>(segments) + 2;
    const std::size_t deltas = starts + 2 * static_cast<std::size_t>(segments);
    const std::size_t ranges = deltas + 2 * static_cast<std::size_t>(segments);
    for (std::uint16_t s = 0; s < segments && reader.ok(); ++s) {
        const std::uint16_t end = reader.u16At(ends + 2 * static_cast<std::size_t>(s));
        if (codepoint > end) {
            continue;
        }
        const std::uint16_t start = reader.u16At(starts + 2 * static_cast<std::size_t>(s));
        if (codepoint < start) {
            return 0;
        }
        const auto delta =
            static_cast<std::uint16_t>(reader.u16At(deltas + 2 * static_cast<std::size_t>(s)));
        const std::size_t rangeAddress = ranges + 2 * static_cast<std::size_t>(s);
        const std::uint16_t range = reader.u16At(rangeAddress);
        if (range == 0) {
            return static_cast<std::uint16_t>(codepoint + delta);
        }
        const std::uint16_t glyph = reader.u16At(rangeAddress + range + 2 * (codepoint - start));
        return glyph == 0 ? 0 : static_cast<std::uint16_t>(glyph + delta);
    }
    return 0;
}

std::uint16_t FontFile::advance(std::uint16_t glyph) const {
    if (metricCount_ == 0) {
        return 0;
    }
    const std::uint16_t index =
        glyph < metricCount_ ? glyph : static_cast<std::uint16_t>(metricCount_ - 1);
    BinaryReader reader(bytes_);
    return reader.u16At(tables_.hmtx + 4 * static_cast<std::size_t>(index));
}

std::size_t FontFile::glyphOffset(std::uint16_t glyph, std::size_t &length) const {
    BinaryReader reader(bytes_);
    std::size_t start = 0;
    std::size_t end = 0;
    if (longOffsets_) {
        reader.seek(tables_.loca + 4 * static_cast<std::size_t>(glyph));
        start = reader.u32();
        end = reader.u32();
    } else {
        reader.seek(tables_.loca + 2 * static_cast<std::size_t>(glyph));
        start = 2 * static_cast<std::size_t>(reader.u16());
        end = 2 * static_cast<std::size_t>(reader.u16());
    }
    length = reader.ok() && end > start ? end - start : 0;
    return tables_.glyf + start;
}

void FontFile::outline(std::uint16_t glyph, double scale, render::VectorPath &path) const {
    const double transform[6] = {scale, 0, 0, -scale, 0, 0};
    appendOutline(glyph, transform, path, 0);
}
}
