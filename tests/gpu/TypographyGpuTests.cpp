#include "../support/ColorLab.h"
#include "../support/PixelOracle.h"
#include "../support/TextScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "backends/metal/MetalBackend.h"

#include <algorithm>
#include <iostream>

int main() {
    using namespace prism;
    auto created = backends::MetalDevice::create();
    if (!created) {
        std::cout << "No native GPU device available\n";
        return 77;
    }
    auto &device = static_cast<backends::MetalDevice &>(*created);
    test::TextScene scene;
    render::DrawList list;
    scene.build(list);
    const hal::Extent extent{192, 160};
    backends::CpuRefSurface reference({extent});
    auto gpu = device.createRenderSurface({extent});
    const auto view = test::viewOf(list, scene.engine->textures());
    const auto expected = test::renderFull(reference, view);
    const auto actual = test::renderFull(*gpu, view);
    std::vector<test::Pixel> a(actual.size() / 4);
    std::vector<test::Pixel> b(a.size());
    double worst = 0;
    const auto clamp = [](double v) { return std::clamp(v, 0.0, 1.0); };
    for (std::size_t i = 0; i < a.size(); ++i) {
        a[i] = {actual[i * 4], actual[i * 4 + 1], actual[i * 4 + 2], actual[i * 4 + 3]};
        b[i] = {expected[i * 4], expected[i * 4 + 1], expected[i * 4 + 2], expected[i * 4 + 3]};
        worst = std::max(
            worst,
            test::deltaE2000(test::labFromLinear(clamp(a[i][0]), clamp(a[i][1]), clamp(a[i][2])),
                             test::labFromLinear(clamp(b[i][0]), clamp(b[i][1]), clamp(b[i][2]))));
    }
    const double psnr = test::comparePixels(a, b, 1.0).psnr;
    std::cout << "Typography parity: PSNR " << psnr << " dB, DeltaE " << worst << '\n';
    if (psnr < 52 || worst > 0.5 || list.primitives().size() != 30) {
        std::cerr << "GPU typography contract failed\n";
        return 1;
    }
    return 0;
}
