#include "text/GlyphRasterizer.h"
#include "text/NativeFace.h"
#include "text/TextShaper.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <vector>

namespace {
using namespace prism;

std::optional<text::FontFile> load(const char *path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                    std::istreambuf_iterator<char>());
    return text::FontFile::parse(std::move(bytes));
}

bool checkKerning(const text::FontFile &font) {
    text::TextShaper shaper({&font});
    const auto &layout = shaper.shape({"AV", 100});
    const auto a = font.glyphIndex('A');
    const double nominal = font.advance(a) * 100.0 / font.metrics().unitsPerEm;
    const double shaped = layout.glyphs.front().advance;
    std::cout << "Kerned A before V: " << shaped << " vs nominal " << nominal << '\n';
    return layout.glyphs.size() == 2 && layout.glyphs.front().glyph == a && shaped < nominal - 1;
}

bool checkArabic(const text::FontFile &font) {
    text::TextShaper shaper({&font});
    const std::u32string word = U"\u0633\u0644\u0627\u0645";
    const auto &layout = shaper.shape({"\u0633\u0644\u0627\u0645", 48});
    int contextual = 0;
    for (const auto &glyph : layout.glyphs) {
        bool isolated = false;
        for (const char32_t cp : word) {
            isolated = isolated || glyph.glyph == font.glyphIndex(cp);
        }
        contextual += isolated ? 0 : 1;
    }
    const bool visualOrder =
        layout.glyphs.size() > 1 && layout.glyphs.front().cluster > layout.glyphs.back().cluster;
    std::cout << "Arabic glyphs " << layout.glyphs.size() << ", contextual forms " << contextual
              << ", visual order " << visualOrder << '\n';
    return contextual >= 2 && visualOrder;
}

bool checkHinting(const text::FontFile &font) {
    text::GlyphBitmap bitmap;
    text::rasterizeAlpha(font, font.glyphIndex('H'), 11, 0, bitmap);
    const auto rowPeak = [&](std::uint32_t y) {
        float peak = 0;
        for (std::uint32_t x = 0; x < bitmap.width; ++x) {
            peak = std::max(peak, bitmap.texels[static_cast<std::size_t>(y) * bitmap.width + x]);
        }
        return peak;
    };
    const float top = rowPeak(0);
    const float bottom = rowPeak(bitmap.height - 1);
    std::cout << "Hinted H at 11 px: " << bitmap.width << "x" << bitmap.height << ", top row "
              << top << ", baseline row " << bottom << '\n';
    return top >= 0.95f && bottom >= 0.95f;
}

bool checkPhases(const text::FontFile &font) {
    std::vector<double> centroids;
    for (std::uint32_t phase = 0; phase < text::subpixelPhases; ++phase) {
        text::GlyphBitmap bitmap;
        text::rasterizeAlpha(font, font.glyphIndex('l'), 11, phase, bitmap);
        double mass = 0;
        double moment = 0;
        for (std::uint32_t y = 0; y < bitmap.height; ++y) {
            for (std::uint32_t x = 0; x < bitmap.width; ++x) {
                const double v = bitmap.texels[static_cast<std::size_t>(y) * bitmap.width + x];
                mass += v;
                moment += v * (bitmap.left + x + 0.5);
            }
        }
        centroids.push_back(moment / mass);
    }
    double worst = 0;
    for (std::size_t i = 1; i < centroids.size(); ++i) {
        worst = std::max(worst, std::abs(centroids[i] - centroids[i - 1] - 0.25));
    }
    std::cout << "Subpixel phase centroid steps deviate from 0.25 px by at most " << worst << '\n';
    return worst < 0.05;
}
}

int main() {
    if (!text::nativeTextAvailable()) {
        return 77;
    }
    const auto arial = load("/System/Library/Fonts/Supplemental/Arial.ttf");
    const auto arabic = load("/System/Library/Fonts/Supplemental/Mishafi.ttf");
    if (!arial || !arabic) {
        return 77;
    }
    if (arial->native() == nullptr || arabic->native() == nullptr) {
        return 1;
    }
    const bool ok =
        checkKerning(*arial) && checkArabic(*arabic) && checkHinting(*arial) && checkPhases(*arial);
    return ok ? 0 : 1;
}
