#pragma once

#include "../support/ReferenceScene.h"
#include "../support/TestFont.h"
#include "text/FontEngine.h"

#include <memory>

namespace prism::test {
struct TextScene {
    std::unique_ptr<text::FontFile> font;
    std::unique_ptr<text::TextShaper> shaper;
    std::unique_ptr<text::FontEngine> engine;

    TextScene() {
        font = std::make_unique<text::FontFile>(*text::FontFile::parse(buildTestFont()));
        shaper =
            std::make_unique<text::TextShaper>(std::vector<const text::FontFile *>{font.get()});
        engine = std::make_unique<text::FontEngine>(std::vector<const text::FontFile *>{font.get()},
                                                    0, 256);
    }

    void build(render::DrawList &list) {
        list.clear();
        list.roundedRect({0, 0, 192, 160}, {}, render::packColor(250, 248, 240, 255));
        list.roundedRect({0, 80, 192, 160}, {}, render::packColor(20, 24, 32, 255));
        engine->beginFrame();
        const auto dark = render::packColor(10, 10, 20, 255);
        const auto light = render::packColor(240, 240, 230, 255);
        engine->draw(list, shaper->shape({"AV fi 0123", 12}), {4.25f, 4}, {dark}, 12, 1);
        engine->draw(list, shaper->shape({"AV fi 0123", 12}), {4, 24}, {dark, true}, 12, 1);
        engine->draw(list, shaper->shape({"AV fi", 40}), {4, 36}, {dark}, 40, 1);
        engine->draw(list, shaper->shape({"AV fi 0123", 12}), {4.5f, 84}, {light}, 12, 1);
        engine->draw(list, shaper->shape({"VA if", 36}), {4, 104}, {light}, 36, 1);
    }
};
}
