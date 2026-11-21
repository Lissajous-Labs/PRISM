#include "../support/ColorLab.h"
#include "../support/GradientScene.h"
#include "../support/PixelOracle.h"
#include "../support/ReferenceScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "backends/metal/MetalBackend.h"

#include <algorithm>
#include <iostream>
#include <vector>

int main() {
    using namespace prism;
    auto created = backends::MetalDevice::create();
    if (!created) {
        std::cout << "No native GPU device available\n";
        return 77;
    }
    auto &device = static_cast<backends::MetalDevice &>(*created);
    constexpr std::uint32_t width = 256;
    test::GradientScene scene(width);
    const auto view = test::viewOf(scene.list, {}, scene.table.rows());
    backends::CpuRefSurface reference({{width, 128}});
    auto gpu = device.createRenderSurface({{width, 128}});
    const auto expected = test::renderFull(reference, view);
    const auto actual = test::renderFull(*gpu, view);
    std::vector<test::Pixel> a(expected.size() / 4);
    std::vector<test::Pixel> b(expected.size() / 4);
    double worstDeltaE = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        a[i] = {actual[i * 4], actual[i * 4 + 1], actual[i * 4 + 2], actual[i * 4 + 3]};
        b[i] = {expected[i * 4], expected[i * 4 + 1], expected[i * 4 + 2], expected[i * 4 + 3]};
        worstDeltaE =
            std::max(worstDeltaE, test::deltaE2000(test::labFromLinear(a[i][0], a[i][1], a[i][2]),
                                                   test::labFromLinear(b[i][0], b[i][1], b[i][2])));
    }
    const auto difference = test::comparePixels(a, b);
    std::vector<std::uint8_t> cpuBytes(expected.size());
    std::vector<std::uint8_t> gpuBytes(expected.size());
    if (!reference.readbackPresented(cpuBytes, 3) || !gpu->readbackPresented(gpuBytes, 3)) {
        return 1;
    }
    std::size_t equal = 0;
    int worstByte = 0;
    for (std::size_t i = 0; i < cpuBytes.size(); ++i) {
        const int delta = std::abs(static_cast<int>(cpuBytes[i]) - static_cast<int>(gpuBytes[i]));
        equal += delta == 0 ? 1 : 0;
        worstByte = std::max(worstByte, delta);
    }
    const double agreement = static_cast<double>(equal) / static_cast<double>(cpuBytes.size());
    std::cout << "Gradient parity: PSNR " << difference.psnr << " dB, DeltaE " << worstDeltaE
              << ", presented agreement " << agreement << ", worst byte " << worstByte << '\n';
    if (difference.psnr < 52 || worstDeltaE > 0.5 || agreement < 0.99 || worstByte > 2) {
        std::cerr << "Gradient GPU contract failed\n";
        return 1;
    }
    return 0;
}
