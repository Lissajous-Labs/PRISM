#include "text/TextShaper.h"

#include "math/Hash64.h"
#include "text/BinaryReader.h"
#include "text/LineBreak.h"
#include "text/UtfFastPath.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace prism::text {
namespace {
std::uint64_t requestKey(const ShapeRequest &request, std::uintptr_t chain) noexcept {
    std::uint64_t seed = math::mix64(static_cast<std::uint64_t>(chain));
    seed = math::mix64(seed ^ static_cast<std::uint64_t>(std::lround(request.size * 64.0f)));
    seed = math::mix64(seed ^ static_cast<std::uint64_t>(std::lround(request.maxWidth * 64.0f)));
    seed = math::mix64(seed ^ static_cast<std::uint64_t>(request.direction));
    for (const std::uint32_t feature : request.features) {
        seed = math::mix64(seed ^ feature);
    }
    return math::hash64(request.text, seed);
}
}

TextShaper::TextShaper(std::vector<const FontFile *> fallbackChain, std::size_t cacheCapacity)
    : fonts_(std::move(fallbackChain)), capacity_(cacheCapacity) {}

std::uint8_t TextShaper::fontFor(char32_t codepoint) const {
    for (std::size_t i = 0; i < fonts_.size(); ++i) {
        if (fonts_[i]->glyphIndex(codepoint) != 0) {
            return static_cast<std::uint8_t>(i);
        }
    }
    return 0;
}

void TextShaper::itemize(std::uint32_t begin, std::uint32_t end, std::vector<Run> &runs) const {
    runs.clear();
    for (std::uint32_t i = begin; i < end; ++i) {
        const std::uint8_t level = bidi_.levels[i];
        const std::uint8_t font = fontFor(codepoints_[i]);
        if (!runs.empty() && runs.back().level == level && runs.back().font == font &&
            runs.back().end == i) {
            runs.back().end = i + 1;
        } else {
            runs.push_back({i, i + 1, level, font});
        }
    }
}

void TextShaper::shapeRun(const Run &run, const ShapeRequest &request,
                          std::vector<PositionedGlyph> &glyphs) const {
    const FontFile &font = *fonts_[run.font];
    std::vector<ShapedGlyph> shaped;
    for (std::uint32_t i = run.begin; i < run.end; ++i) {
        shaped.push_back({font.glyphIndex(codepoints_[i]), i});
    }
    std::vector<std::uint32_t> features{tag("liga"), tag("kern")};
    features.insert(features.end(), request.features.begin(), request.features.end());
    font.layout().substitute(features, shaped);
    const float scale = request.size / font.metrics().unitsPerEm;
    const std::size_t first = glyphs.size();
    for (std::size_t g = 0; g < shaped.size(); ++g) {
        float advance = font.advance(shaped[g].glyph) * scale;
        if (g + 1 < shaped.size()) {
            advance += static_cast<float>(
                           font.layout().pairAdjustment(shaped[g].glyph, shaped[g + 1].glyph)) *
                       scale;
        }
        glyphs.push_back({shaped[g].glyph, run.font, shaped[g].cluster, 0, 0, advance});
    }
    if (run.level % 2 == 1) {
        std::reverse(glyphs.begin() + static_cast<std::ptrdiff_t>(first), glyphs.end());
    }
}

void TextShaper::layout(const ShapeRequest &request, TextLayout &result) {
    result = TextLayout{};
    result.asciiFastPath = isAscii(request.text);
    decodeUtf8(request.text, codepoints_);
    const auto count = static_cast<std::uint32_t>(codepoints_.size());
    if (result.asciiFastPath && request.direction != ParagraphDirection::rightToLeft) {
        bidi_.paragraphLevel = 0;
        bidi_.levels.assign(count, 0);
    } else {
        resolveBidi(codepoints_, request.direction, bidi_);
    }
    const FontFile &primary = *fonts_.front();
    const float scale = request.size / primary.metrics().unitsPerEm;
    result.ascent = primary.metrics().ascender * scale;
    result.descent = -primary.metrics().descender * scale;
    const float lineHeight = result.ascent + result.descent + primary.metrics().lineGap * scale;
    std::vector<Run> runs;
    std::vector<PositionedGlyph> measured;
    itemize(0, count, runs);
    for (const Run &run : runs) {
        shapeRun(run, request, measured);
    }
    std::vector<float> clusterAdvance(count + 1, 0.0f);
    for (const PositionedGlyph &glyph : measured) {
        clusterAdvance[glyph.cluster] += glyph.advance;
    }
    std::vector<BreakAction> breaks;
    findBreaks(codepoints_, breaks);
    std::uint32_t lineStart = 0;
    std::uint32_t lastBreak = 0;
    float width = 0;
    float widthAtBreak = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> lines;
    for (std::uint32_t i = 0; i < count; ++i) {
        width += clusterAdvance[i];
        if (breaks[i + 1] == BreakAction::mandatory) {
            lines.emplace_back(lineStart, i + 1);
            lineStart = i + 1;
            lastBreak = lineStart;
            width = 0;
            widthAtBreak = 0;
            continue;
        }
        if (request.maxWidth > 0 && width > request.maxWidth && lastBreak > lineStart) {
            lines.emplace_back(lineStart, lastBreak);
            lineStart = lastBreak;
            width -= widthAtBreak;
        }
        if (breaks[i + 1] == BreakAction::allowed) {
            lastBreak = i + 1;
            widthAtBreak = width;
        }
    }
    if (lineStart < count || lines.empty()) {
        lines.emplace_back(lineStart, count);
    }
    float baseline = result.ascent;
    for (const auto &[begin, end] : lines) {
        itemize(begin, end, runs);
        std::vector<std::size_t> order(runs.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        std::uint8_t highest = 0;
        for (const Run &run : runs) {
            highest = std::max(highest, run.level);
        }
        for (int level = highest; level >= 1; --level) {
            for (std::size_t i = 0; i < order.size(); ++i) {
                if (runs[order[i]].level < level) {
                    continue;
                }
                std::size_t stop = i;
                while (stop < order.size() && runs[order[stop]].level >= level) {
                    ++stop;
                }
                std::reverse(order.begin() + static_cast<std::ptrdiff_t>(i),
                             order.begin() + static_cast<std::ptrdiff_t>(stop));
                i = stop;
            }
        }
        LineInfo info{static_cast<std::uint32_t>(result.glyphs.size()), 0, 0, baseline};
        float pen = 0;
        for (const std::size_t index : order) {
            const std::size_t first = result.glyphs.size();
            shapeRun(runs[index], request, result.glyphs);
            for (std::size_t g = first; g < result.glyphs.size(); ++g) {
                const bool trailingSpace = codepoints_[result.glyphs[g].cluster] == ' ' &&
                                           result.glyphs[g].cluster + 1 == end;
                result.glyphs[g].x = pen;
                result.glyphs[g].y = baseline;
                pen += trailingSpace ? 0 : result.glyphs[g].advance;
            }
        }
        info.glyphCount = static_cast<std::uint32_t>(result.glyphs.size()) - info.firstGlyph;
        info.width = pen;
        result.width = std::max(result.width, pen);
        result.lines.push_back(info);
        baseline += lineHeight;
    }
    result.height = lineHeight * static_cast<float>(result.lines.size());
}

const TextLayout &TextShaper::shape(const ShapeRequest &request) {
    const std::uint64_t key = requestKey(request, reinterpret_cast<std::uintptr_t>(this));
    const auto found = cache_.find(key);
    if (found != cache_.end()) {
        return found->second;
    }
    ++misses_;
    if (cache_.size() >= capacity_) {
        cache_.erase(cache_.begin());
    }
    TextLayout &entry = cache_[key];
    layout(request, entry);
    return entry;
}
}
