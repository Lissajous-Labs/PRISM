#include "../support/PixelOracle.h"
#include "../support/ReferenceScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "backends/metal/MetalBackend.h"
#include "render/GradientTable.h"
#include "widgets/GpuFilterCurve.h"
#include "widgets/GpuLoudnessMeter.h"
#include "widgets/GpuScope.h"
#include "widgets/GpuSpectrum.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>

int main() {
    using namespace prism;
    auto created = backends::MetalDevice::create();
    if (!created) {
        std::cout << "No native GPU device available\n";
        return 77;
    }
    auto &device = static_cast<backends::MetalDevice &>(*created);
    constexpr double rate = 48000;
    std::vector<float> signal(1 << 17);
    for (std::size_t n = 0; n < signal.size(); ++n) {
        signal[n] = static_cast<float>(0.4 * std::sin(2 * std::numbers::pi * 220 * n / rate) +
                                       0.2 * std::sin(2 * std::numbers::pi * 3150 * n / rate));
    }
    widgets::GpuScope scope(256, 1 << 18);
    scope.append(signal);
    scope.build(48);
    widgets::GpuSpectrum spectrum(256, rate);
    if (not spectrum.analyze(signal, 1.0 / 60)) {
        return 1;
    }
    spectrum.buildStrip(-90, 0);
    for (int row = 0; row < 40; ++row) {
        spectrum.appendWaterfallRow(-90, 0);
    }
    widgets::FilterResponse response(rate);
    response.add(widgets::designBiquad(widgets::FilterShape::peaking, rate, 1200, 1.5, 8));
    response.add(widgets::designBiquad(widgets::FilterShape::highpass, rate, 80, 0.7));
    widgets::GpuFilterCurve curve;
    curve.build(response, 20, 20000, -24, 24);
    telemetry::LoudnessMeter meter(rate);
    meter.process(signal.data(), signal.data(), signal.size());
    render::GradientTable gradients;
    const std::array<render::TextureView, 3> textures{scope.strip(), spectrum.strip(),
                                                      spectrum.waterfall()};
    render::DrawList list;
    list.roundedRect({0, 0, 512, 256}, {}, render::packColor(14, 16, 22, 255));
    scope.draw(list, {8, 8, 248, 120}, 0, 1.5f, render::packColor(80, 255, 160, 255));
    spectrum.draw(list, {264, 8, 504, 120}, render::packColor(255, 120, 40, 255));
    spectrum.drawWaterfall(list, {8, 136, 248, 248}, 2, render::packColor(255, 255, 255, 255));
    curve.draw(list, {264, 136, 472, 248}, 2, render::packColor(255, 200, 60, 255));
    widgets::drawLoudnessMeter(
        list, {480, 136, 504, 248}, meter.reading(), -1,
        {-60, 0, 6, render::packColor(30, 30, 36, 255), render::packColor(60, 220, 120, 255),
         render::packColor(200, 200, 200, 255), render::packColor(255, 80, 60, 255)});
    const render::FrameView view{list.primitives(), list.payload(),     list.transforms(),
                                 list.clips(),      gradients.rows(),   textures,
                                 list.paths(),      list.pathSegments()};
    const hal::Extent extent{512, 256};
    backends::CpuRefSurface reference({extent});
    auto gpu = device.createRenderSurface({extent});
    auto &metal = static_cast<backends::MetalSurface &>(*gpu);
    const auto expected = test::renderFull(reference, view);
    const auto actual = test::renderFull(*gpu, view);
    hal::DamageRegion full({0, 0, 512, 256});
    const auto fastest = [&](const render::FrameView &frame) {
        double best = 1e9;
        for (int run = 0; run < 200; ++run) {
            if (!gpu->render({frame, &full, {0, 0, 0, 1}})) {
                return 1e9;
            }
            best = std::min(best, metal.lastGpuMilliseconds());
        }
        return best;
    };
    render::DrawList backdrop;
    backdrop.roundedRect({0, 0, 512, 256}, {}, render::packColor(14, 16, 22, 255));
    const double baseline = fastest(test::viewOf(backdrop));
    const double total = fastest(view);
    const double best = total - baseline;
    std::vector<test::Pixel> a(actual.size() / 4);
    std::vector<test::Pixel> b(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        a[i] = {actual[i * 4], actual[i * 4 + 1], actual[i * 4 + 2], actual[i * 4 + 3]};
        b[i] = {expected[i * 4], expected[i * 4 + 1], expected[i * 4 + 2], expected[i * 4 + 3]};
    }
    const double psnr = test::comparePixels(a, b, 1.0).psnr;
    std::cout << "Visualizer parity PSNR " << psnr << " dB, GPU time " << best << " ms for "
              << list.primitives().size() << " primitives\n";
    if (psnr < 50 || (best >= 0.15 && !device.virtualized())) {
        std::cerr << "Visualizer contract failed\n";
        return 1;
    }

    render::DrawList glowList;
    glowList.roundedRect({0, 0, 512, 256}, {}, render::packColor(8, 8, 10, 255));
    spectrum.draw(glowList, {16, 40, 496, 240}, render::packColor(255, 230, 120, 255), 2.0f);
    const auto glowView = test::viewOf(glowList);
    render::BloomSettings bloom{
        .threshold = 0.35f, .knee = 0.5f, .intensity = 0.6f, .levels = 4, .karis = true};
    std::vector<float> cpuGlow(512 * 256 * 4);
    std::vector<float> gpuGlow(cpuGlow.size());
    std::vector<float> plain(cpuGlow.size());
    if (!reference.render({glowView, &full, {0, 0, 0, 1}, &bloom}) ||
        !reference.readback(cpuGlow) || !gpu->render({glowView, &full, {0, 0, 0, 1}, &bloom}) ||
        !gpu->readback(gpuGlow) || !gpu->render({glowView, &full, {0, 0, 0, 1}}) ||
        !gpu->readback(plain)) {
        return 1;
    }
    std::vector<test::Pixel> glowA(gpuGlow.size() / 4);
    std::vector<test::Pixel> glowB(glowA.size());
    double halo = 0;
    for (std::size_t i = 0; i < glowA.size(); ++i) {
        glowA[i] = {gpuGlow[i * 4], gpuGlow[i * 4 + 1], gpuGlow[i * 4 + 2], gpuGlow[i * 4 + 3]};
        glowB[i] = {cpuGlow[i * 4], cpuGlow[i * 4 + 1], cpuGlow[i * 4 + 2], cpuGlow[i * 4 + 3]};
        halo = std::max(halo, static_cast<double>(gpuGlow[i * 4] - plain[i * 4]));
    }
    const double glowPsnr = test::comparePixels(glowA, glowB, 1.0).psnr;
    std::cout << "Bloomed spectrum parity PSNR " << glowPsnr << " dB, peak halo gain " << halo
              << '\n';
    return glowPsnr >= 50 && halo > 0.01 ? 0 : 1;
}
