#include "text/OpenTypeLayout.h"

#include "text/BinaryReader.h"

#include <algorithm>
#include <bit>

namespace prism::text {
namespace {
constexpr std::uint16_t singleSubstitution = 1;
constexpr std::uint16_t ligatureSubstitution = 4;
constexpr std::uint16_t substitutionExtension = 7;
constexpr std::uint16_t pairPositioning = 2;
constexpr std::uint16_t positioningExtension = 9;

std::size_t valueRecordSize(std::uint16_t format) noexcept {
    return 2 * static_cast<std::size_t>(std::popcount(format));
}

std::int32_t xAdvanceOf(BinaryReader &reader, std::size_t record, std::uint16_t format) {
    if ((format & 0x4u) == 0) {
        return 0;
    }
    const std::size_t before =
        static_cast<std::size_t>(std::popcount(static_cast<unsigned>(format & 0x3u)));
    reader.seek(record + 2 * before);
    return reader.i16();
}
}

void OpenTypeLayout::parse(std::span<const std::uint8_t> font, std::size_t gsubOffset,
                           std::size_t gposOffset) {
    data_ = font;
    substitutions_.clear();
    pairLookups_.clear();
    if (gsubOffset != 0) {
        collect(gsubOffset, false);
    }
    if (gposOffset != 0) {
        collect(gposOffset, true);
    }
}

void OpenTypeLayout::collect(std::size_t table, bool positioning) {
    BinaryReader reader(data_, table + 6);
    const std::size_t featureList = table + reader.u16();
    const std::size_t lookupList = table + reader.u16();
    const std::uint16_t featureCount = reader.u16At(featureList);
    for (std::uint16_t f = 0; f < featureCount && reader.ok(); ++f) {
        reader.seek(featureList + 2 + 6 * static_cast<std::size_t>(f));
        const std::uint32_t featureTag = reader.u32();
        const std::size_t feature = featureList + reader.u16();
        const std::uint16_t lookupCount = reader.u16At(feature + 2);
        for (std::uint16_t l = 0; l < lookupCount && reader.ok(); ++l) {
            const std::uint16_t lookupIndex =
                reader.u16At(feature + 4 + 2 * static_cast<std::size_t>(l));
            const std::size_t lookup =
                lookupList +
                reader.u16At(lookupList + 2 + 2 * static_cast<std::size_t>(lookupIndex));
            std::uint16_t type = reader.u16At(lookup);
            const std::uint16_t subtableCount = reader.u16At(lookup + 4);
            for (std::uint16_t s = 0; s < subtableCount && reader.ok(); ++s) {
                std::size_t subtable =
                    lookup + reader.u16At(lookup + 6 + 2 * static_cast<std::size_t>(s));
                std::uint16_t actual = type;
                if (type == (positioning ? positioningExtension : substitutionExtension)) {
                    reader.seek(subtable + 2);
                    actual = reader.u16();
                    subtable += reader.u32();
                }
                if (positioning && actual == pairPositioning && featureTag == tag("kern")) {
                    pairLookups_.push_back(subtable);
                } else if (!positioning &&
                           (actual == singleSubstitution || actual == ligatureSubstitution)) {
                    substitutions_.push_back({featureTag, subtable, actual});
                }
            }
        }
    }
}

int OpenTypeLayout::coverageIndex(std::size_t coverage, std::uint16_t glyph) const {
    BinaryReader reader(data_, coverage);
    const std::uint16_t format = reader.u16();
    const std::uint16_t count = reader.u16();
    if (format == 1) {
        for (std::uint16_t i = 0; i < count && reader.ok(); ++i) {
            if (reader.u16() == glyph) {
                return i;
            }
        }
    } else if (format == 2) {
        for (std::uint16_t i = 0; i < count && reader.ok(); ++i) {
            const std::uint16_t start = reader.u16();
            const std::uint16_t end = reader.u16();
            const std::uint16_t startIndex = reader.u16();
            if (glyph >= start && glyph <= end) {
                return startIndex + glyph - start;
            }
        }
    }
    return -1;
}

std::uint16_t OpenTypeLayout::classOf(std::size_t classDef, std::uint16_t glyph) const {
    BinaryReader reader(data_, classDef);
    const std::uint16_t format = reader.u16();
    if (format == 1) {
        const std::uint16_t start = reader.u16();
        const std::uint16_t count = reader.u16();
        if (glyph >= start && glyph < start + count) {
            return reader.u16At(classDef + 6 + 2 * static_cast<std::size_t>(glyph - start));
        }
    } else if (format == 2) {
        const std::uint16_t count = reader.u16();
        for (std::uint16_t i = 0; i < count && reader.ok(); ++i) {
            const std::uint16_t start = reader.u16();
            const std::uint16_t end = reader.u16();
            const std::uint16_t value = reader.u16();
            if (glyph >= start && glyph <= end) {
                return value;
            }
        }
    }
    return 0;
}

bool OpenTypeLayout::applySingle(std::size_t subtable, ShapedGlyph &glyph) const {
    BinaryReader reader(data_, subtable);
    const std::uint16_t format = reader.u16();
    const int index = coverageIndex(subtable + reader.u16(), glyph.glyph);
    if (index < 0) {
        return false;
    }
    if (format == 1) {
        glyph.glyph = static_cast<std::uint16_t>(glyph.glyph + reader.i16());
        return true;
    }
    const std::uint16_t count = reader.u16();
    if (format == 2 && index < count) {
        glyph.glyph = reader.u16At(subtable + 6 + 2 * static_cast<std::size_t>(index));
        return true;
    }
    return false;
}

bool OpenTypeLayout::applyLigature(std::size_t subtable, std::vector<ShapedGlyph> &glyphs,
                                   std::size_t index) const {
    BinaryReader reader(data_, subtable + 2);
    const int coverage = coverageIndex(subtable + reader.u16(), glyphs[index].glyph);
    if (coverage < 0) {
        return false;
    }
    const std::size_t set =
        subtable + reader.u16At(subtable + 6 + 2 * static_cast<std::size_t>(coverage));
    const std::uint16_t ligatureCount = reader.u16At(set);
    for (std::uint16_t l = 0; l < ligatureCount && reader.ok(); ++l) {
        const std::size_t ligature = set + reader.u16At(set + 2 + 2 * static_cast<std::size_t>(l));
        reader.seek(ligature);
        const std::uint16_t result = reader.u16();
        const std::uint16_t components = reader.u16();
        if (components == 0 || index + components > glyphs.size()) {
            continue;
        }
        bool matches = true;
        for (std::uint16_t c = 1; c < components && matches; ++c) {
            matches = reader.u16() == glyphs[index + c].glyph;
        }
        if (matches) {
            glyphs[index].glyph = result;
            glyphs.erase(glyphs.begin() + static_cast<std::ptrdiff_t>(index + 1),
                         glyphs.begin() + static_cast<std::ptrdiff_t>(index + components));
            return true;
        }
    }
    return false;
}

void OpenTypeLayout::substitute(std::span<const std::uint32_t> features,
                                std::vector<ShapedGlyph> &glyphs) const {
    for (const LookupRef &lookup : substitutions_) {
        if (std::find(features.begin(), features.end(), lookup.feature) == features.end()) {
            continue;
        }
        for (std::size_t i = 0; i < glyphs.size(); ++i) {
            if (lookup.type == singleSubstitution) {
                static_cast<void>(applySingle(lookup.subtable, glyphs[i]));
            } else {
                static_cast<void>(applyLigature(lookup.subtable, glyphs, i));
            }
        }
    }
}

std::int32_t OpenTypeLayout::pairValue(std::size_t subtable, std::uint16_t first,
                                       std::uint16_t second) const {
    BinaryReader reader(data_, subtable);
    const std::uint16_t format = reader.u16();
    const int coverage = coverageIndex(subtable + reader.u16(), first);
    if (coverage < 0) {
        return 0;
    }
    reader.seek(subtable + 4);
    const std::uint16_t format1 = reader.u16();
    const std::uint16_t format2 = reader.u16();
    const std::size_t record = 2 + valueRecordSize(format1) + valueRecordSize(format2);
    if (format == 1) {
        const std::size_t pairSet =
            subtable + reader.u16At(subtable + 10 + 2 * static_cast<std::size_t>(coverage));
        const std::uint16_t count = reader.u16At(pairSet);
        for (std::uint16_t i = 0; i < count && reader.ok(); ++i) {
            const std::size_t entry = pairSet + 2 + record * i;
            if (reader.u16At(entry) == second) {
                return xAdvanceOf(reader, entry + 2, format1);
            }
        }
    } else if (format == 2) {
        reader.seek(subtable + 8);
        const std::size_t classDef1 = subtable + reader.u16();
        const std::size_t classDef2 = subtable + reader.u16();
        reader.skip(2);
        const std::uint16_t class2Count = reader.u16();
        const std::size_t base = subtable + 16;
        const std::size_t entry =
            base + (static_cast<std::size_t>(classOf(classDef1, first)) * class2Count +
                    classOf(classDef2, second)) *
                       (record - 2);
        return xAdvanceOf(reader, entry, format1);
    }
    return 0;
}

std::int32_t OpenTypeLayout::pairAdjustment(std::uint16_t first, std::uint16_t second) const {
    for (const std::size_t subtable : pairLookups_) {
        const std::int32_t value = pairValue(subtable, first, second);
        if (value != 0) {
            return value;
        }
    }
    return 0;
}
}
