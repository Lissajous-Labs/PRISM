#include "../support/PixelOracle.h"
#include "../support/TextScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "backends/metal/MetalBackend.h"

#include <algorithm>
#include <cmath>
#include <iostream>

int main() {
    using namespace prism;
    auto created = backends::MetalDevice::create();
    if (!created) {
        return 77;
    }

    auto &device = static_cast<backends::MetalDevice &>(*created);
    test::TextScene scene;
    scene.engine = std::make_unique<text::FontEngine>(
        std::vector<const text::FontFile *>{scene.font.get()}, 0, 1024);
    const hal::Extent extent{240, 80};
    auto surface = device.createRenderSurface({extent});
    auto &metal = static_cast<backends::MetalSurface &>(*surface);
    backends::CpuRefSurface reference({extent});

    const auto frame = [&](std::string_view text) {
        render::DrawList list;
        scene.engine->beginFrame();
        scene.engine->draw(list, scene.shaper->shape({text, 40}), {4, 8}, {test::white}, 40, 1);
        return list;
    };

    std::size_t warm = 0;
    for (int i = 0; i < 3; ++i) {
        const auto list = frame("AV");
        if (test::renderFull(*surface, test::viewOf(list, scene.engine->textures())).empty()) {
            return 1;
        }
        warm = std::max(warm, metal.lastUploadBytes());
    }

    std::size_t steady = 0;
    std::vector<float> actual;
    render::DrawList last;
    for (int i = 0; i < 3; ++i) {
        last = frame("AV fi 0");
        actual = test::renderFull(*surface, test::viewOf(last, scene.engine->textures()));
        steady = std::max(steady, metal.lastUploadBytes());
    }

    const auto expected = test::renderFull(reference, test::viewOf(last, scene.engine->textures()));
    double worst = 0;
    for (std::size_t i = 0; i < expected.size() && i < actual.size(); ++i) {
        worst = std::max(worst, static_cast<double>(std::abs(expected[i] - actual[i])));
    }

    const std::size_t atlasBytes = scene.engine->textures()[2].texels.size_bytes();
    std::cout << "Atlas bytes " << atlasBytes << ", first upload " << warm << ", insertion upload "
              << steady << ", max pixel error " << worst << '\n';
    return !actual.empty() && steady * 16 < atlasBytes && worst < 0.01 ? 0 : 1;
}
