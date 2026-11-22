#include "../support/ColorLab.h"
#include "../support/PixelOracle.h"
#include "../support/ReferenceScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "backends/metal/MetalBackend.h"

#include <algorithm>
#include <iostream>
#include <vector>

namespace {
using namespace prism;

void buildEffectScene(render::DrawList &list) {
    list.clear();
    for (int stripe = 0; stripe < 16; ++stripe) {
        const auto shade = static_cast<std::uint8_t>(stripe % 2 == 0 ? 230 : 30);
        list.roundedRect(
            {static_cast<float>(stripe * 12), 0, static_cast<float>(stripe * 12 + 12), 160}, {},
            render::packColor(shade, static_cast<std::uint8_t>(255 - shade), 120, 255));
    }
    list.backdropBlur({8, 8, 60, 70}, {8, 8, 8, 8}, 3);
    list.roundedRect({8, 8, 60, 70}, {8, 8, 8, 8}, render::packColor(255, 255, 255, 40));
    list.backdropBlur({70, 8, 130, 90}, {12, 12, 12, 12}, 10);
    list.backdropBlur({20, 90, 180, 150}, {16, 16, 16, 16}, 24);
    list.backdropBlur({120, 4, 188, 84}, {10, 10, 10, 10}, 20);
    list.capsule({140, 20}, {180, 60}, 2, render::packColor(255, 255, 255, 255));
}

struct Comparison {
    double psnr;
    double deltaE;
};

Comparison compare(const std::vector<float> &actual, const std::vector<float> &expected) {
    std::vector<test::Pixel> a(actual.size() / 4);
    std::vector<test::Pixel> b(actual.size() / 4);
    double worst = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        a[i] = {actual[i * 4], actual[i * 4 + 1], actual[i * 4 + 2], actual[i * 4 + 3]};
        b[i] = {expected[i * 4], expected[i * 4 + 1], expected[i * 4 + 2], expected[i * 4 + 3]};
        const auto clamp = [](double v) { return std::clamp(v, 0.0, 1.0); };
        worst = std::max(
            worst,
            test::deltaE2000(test::labFromLinear(clamp(a[i][0]), clamp(a[i][1]), clamp(a[i][2])),
                             test::labFromLinear(clamp(b[i][0]), clamp(b[i][1]), clamp(b[i][2]))));
    }
    return {test::comparePixels(a, b, 1.0).psnr, worst};
}
}

int main() {
    auto created = backends::MetalDevice::create();
    if (!created) {
        std::cout << "No native GPU device available\n";
        return 77;
    }
    auto &device = static_cast<backends::MetalDevice &>(*created);
    render::DrawList list;
    buildEffectScene(list);
    const hal::Extent extent{192, 160};
    backends::CpuRefSurface reference({extent});
    auto gpu = device.createRenderSurface({extent});
    const auto view = test::viewOf(list);
    const auto blurred = compare(test::renderFull(*gpu, view), test::renderFull(reference, view));
    render::BloomSettings bloom;
    bloom.threshold = 0.8f;
    hal::DamageRegion full({0, 0, 192, 160});
    std::vector<float> cpuBloom(192 * 160 * 4);
    std::vector<float> gpuBloom(192 * 160 * 4);
    if (!reference.render({view, &full, {0, 0, 0, 1}, &bloom}) || !reference.readback(cpuBloom) ||
        !gpu->render({view, &full, {0, 0, 0, 1}, &bloom}) || !gpu->readback(gpuBloom)) {
        return 1;
    }
    const auto bloomed = compare(gpuBloom, cpuBloom);
    hal::DamageRegion partial({100, 100, 140, 140});
    std::vector<float> incremental(192 * 160 * 4);
    if (!gpu->render({view, &partial, {0, 0, 0, 1}}) || !gpu->readback(incremental)) {
        return 1;
    }
    const bool exact = incremental == test::renderFull(*gpu, view);
    std::cout << "Backdrop blur parity: PSNR " << blurred.psnr << " dB, DeltaE " << blurred.deltaE
              << "; bloom parity: PSNR " << bloomed.psnr << " dB, DeltaE " << bloomed.deltaE
              << "; partial exact " << exact << '\n';
    if (blurred.psnr < 52 || blurred.deltaE > 0.5 || bloomed.psnr < 52 || bloomed.deltaE > 0.5 ||
        !exact) {
        std::cerr << "GPU post effect contract failed\n";
        return 1;
    }
    return 0;
}
