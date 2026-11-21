#include "../support/PixelOracle.h"
#include "../support/TextScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "text/GlyphRasterizer.h"
#include "text/MtsdfGenerator.h"

#include <cmath>
#include <iostream>

namespace {
using namespace prism;

bool checkZoomInvariance(test::TextScene &scene) {
    const auto &layout = scene.shaper->shape({"AV fi 0123", 16});
    render::DrawList list;
    std::size_t first = 0;
    bool stable = true;
    for (const double zoom : {0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0}) {
        list.clear();
        scene.engine->beginFrame();
        scene.engine->draw(list, layout, {10, 10}, {test::white, false, true}, 16, zoom);
        if (first == 0) {
            first = scene.engine->rasterizations();
        }
        stable = stable && scene.engine->rasterizations() == first && list.primitives().size() == 7;
    }
    const std::size_t before = scene.engine->rasterizations();
    std::size_t afterFirst = 0;
    for (int frame = 0; frame < 3; ++frame) {
        list.clear();
        scene.engine->beginFrame();
        scene.engine->draw(list, layout, {10, 10}, {test::white}, 14, 1);
        if (frame == 0) {
            afterFirst = scene.engine->rasterizations();
        }
    }
    const std::size_t tierOne = afterFirst - before;
    std::cout << "Distance field rasterizations " << first << ", tier one " << tierOne << '\n';
    return stable && first == 8 && tierOne >= 8 && scene.engine->rasterizations() == afterFirst;
}

double alphaOf(const std::vector<float> &pixels, std::size_t i) { return pixels[i * 4 + 3]; }

bool checkDistanceFieldAccuracy(test::TextScene &scene) {
    const hal::Extent extent{200, 200};
    backends::CpuRefSurface surface({extent});
    render::DrawList field;
    const auto &layout = scene.shaper->shape({"AV", 160});
    scene.engine->draw(field, layout, {10, 10}, {test::white}, 160, 1);
    const auto fieldView = test::viewOf(field, scene.engine->textures());
    const auto fromField = test::renderFull(surface, fieldView, {0, 0, 0, 0});
    render::DrawList vector;
    for (const auto &glyph : layout.glyphs) {
        render::VectorPath outline;
        scene.font->outline(glyph.glyph, 0.16, outline);
        vector.setTransform(
            *vector.addTransform(math::Affine2D::translation(10 + glyph.x, 10 + glyph.y)));
        vector.fillPath(outline, test::white);
    }
    const auto fromVector = test::renderFull(surface, test::viewOf(vector), {0, 0, 0, 0});
    std::vector<test::Pixel> a(fromField.size() / 4);
    std::vector<test::Pixel> b(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        a[i] = {alphaOf(fromField, i), 0, 0, 0};
        b[i] = {alphaOf(fromVector, i), 0, 0, 0};
    }
    const auto difference = test::comparePixels(a, b, 1.0);
    std::cout << "Distance field vs vector: PSNR " << difference.psnr << " dB, max "
              << difference.maximumAbsolute << '\n';
    return difference.psnr > 32 && difference.maximumAbsolute < 0.35;
}

double stemWeight(const std::vector<float> &pixels, double background, double foreground) {
    double sum = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        const double luminance =
            0.2126 * pixels[i] + 0.7152 * pixels[i + 1] + 0.0722 * pixels[i + 2];
        sum += (luminance - background) / (foreground - background);
    }
    return sum;
}

bool checkPolarityBalance(test::TextScene &scene) {
    const hal::Extent extent{120, 20};
    backends::CpuRefSurface surface({extent});
    const auto &layout = scene.shaper->shape({"fi fi fi fi", 11});
    const auto render = [&](float background, std::uint32_t color) {
        render::DrawList list;
        const auto bg = static_cast<std::uint8_t>(background * 255);
        list.roundedRect({0, 0, 120, 20}, {}, render::packColor(bg, bg, bg, 255));
        scene.engine->draw(list, layout, {2, 2}, {color}, 11, 1);
        return test::renderFull(surface, test::viewOf(list, scene.engine->textures()),
                                {0, 0, 0, 1});
    };
    const auto darkOnLight = render(1, render::packColor(0, 0, 0, 255));
    const auto lightOnDark = render(0, render::packColor(255, 255, 255, 255));
    const double dark = stemWeight(darkOnLight, 1, 0);
    const double light = stemWeight(lightOnDark, 0, 1);
    const double imbalance = std::abs(dark - light) / std::max(dark, light);
    std::cout << "Stem weight dark " << dark << ", light " << light << ", imbalance " << imbalance
              << '\n';
    return imbalance < 0.05;
}

bool checkAtlasEviction(const text::FontFile &font) {
    text::GlyphAtlas atlas(64, 1, 1);
    text::GlyphBitmap bitmap;
    const auto *page = atlas.pageCount() == 0 ? nullptr : atlas.page(0).data();
    bool inserted = true;
    for (std::uint32_t round = 0; round < 6; ++round) {
        for (std::uint16_t glyph = 2; glyph < test::glyphs::count; ++glyph) {
            text::rasterizeAlpha(font, glyph, 18, round % 4, bitmap);
            inserted =
                inserted &&
                atlas.insert(glyph * 8u + round, bitmap.width, bitmap.height, bitmap.texels, round)
                    .has_value();
        }
        if (page == nullptr) {
            page = atlas.page(0).data();
        }
    }
    const bool recentKept = atlas.find((test::glyphs::count - 1) * 8u + 5, 6).has_value();
    const bool coldDropped = !atlas.find(2 * 8u, 6).has_value();
    std::cout << "Atlas evictions " << atlas.evictions() << '\n';
    return inserted && atlas.pageCount() == 1 && atlas.page(0).data() == page &&
           atlas.evictions() > 0 && recentKept && coldDropped;
}

bool checkLcdFilter(const text::FontFile &font) {
    text::GlyphBitmap lcd;
    text::GlyphBitmap alpha;
    text::rasterizeLcd(font, test::glyphs::capitalA, 14, 0, lcd);
    text::rasterizeAlpha(font, test::glyphs::capitalA, 14, 0, alpha);
    double lcdSum = 0;
    double alphaSum = 0;
    bool fringe = false;
    for (std::size_t i = 0; i < lcd.texels.size(); i += 4) {
        lcdSum += lcd.texels[i + 3];
        fringe = fringe || std::abs(lcd.texels[i] - lcd.texels[i + 2]) > 0.05;
    }
    for (const float value : alpha.texels) {
        alphaSum += value;
    }
    return lcd.channels == 4 && fringe && std::abs(lcdSum - alphaSum) / alphaSum < 0.02;
}
}

int main() {
    test::TextScene scene;
    if (!checkZoomInvariance(scene) || !checkDistanceFieldAccuracy(scene) ||
        !checkPolarityBalance(scene) || !checkAtlasEviction(*scene.font) ||
        !checkLcdFilter(*scene.font)) {
        std::cerr << "Typography contract failed\n";
        return 1;
    }
    return 0;
}
