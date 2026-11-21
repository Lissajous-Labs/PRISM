#pragma once

#include "render/DrawList.h"
#include "render/GradientTable.h"

#include <array>

namespace prism::test {
struct GradientScene {
    render::GradientTable table;
    render::DrawList list;

    explicit GradientScene(std::uint32_t width) {
        const std::array<render::GradientStop, 2> darkRamp{
            render::GradientStop{0, render::packColor(0, 0, 0, 255)},
            render::GradientStop{1, render::packColor(4, 4, 4, 255)}};
        const std::array<render::GradientStop, 3> vivid{
            render::GradientStop{0, render::packColor(255, 0, 80, 255)},
            render::GradientStop{0.5f, render::packColor(20, 200, 90, 255)},
            render::GradientStop{1, render::packColor(30, 60, 255, 255)}};
        const auto dark = table.add(darkRamp, render::GradientSpace::linearRgb);
        const auto lab = table.add(vivid, render::GradientSpace::oklab);
        const auto lch = table.add(vivid, render::GradientSpace::oklch);
        const float w = static_cast<float>(width);
        list.setLinearGradient(dark, {0, 0}, {w, 0});
        list.roundedRect({0, 0, w, 64}, {}, render::packColor(255, 255, 255, 255));
        list.setLinearGradient(lab, {0, 64}, {w, 96});
        list.roundedRect({0, 64, w, 96}, {6, 6, 6, 6}, render::packColor(255, 255, 255, 255));
        list.setRadialGradient(lch, {w / 4, 112}, {w / 4 + 6, 108}, 14);
        list.roundedRect({w / 4 - 16, 96, w / 4 + 16, 128}, {},
                         render::packColor(255, 255, 255, 255));
        list.setRadialGradient(lab, {w / 2, 112}, {w / 2 + 14, 112}, 14);
        list.capsule({w / 2 - 10, 112}, {w / 2 + 10, 112}, 12,
                     render::packColor(255, 255, 255, 220));
        list.setConicGradient(lch, {3 * w / 4, 112}, 0.3f);
        list.squircle({3 * w / 4 - 16, 96, 3 * w / 4 + 16, 128}, 4,
                      render::packColor(255, 255, 255, 255));
        list.clearGradient();
    }
};
}
