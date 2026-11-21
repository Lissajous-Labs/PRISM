#include "../support/PixelOracle.h"
#include "../support/ReferenceScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "backends/metal/MetalBackend.h"
#include "math/ThermalPalette.h"
#include "render/GradientTable.h"
#include "widgets/GpuWavetable3d.h"

#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
prism::test::Lab lab(const float *rgb) {
    const double x = (0.4124564 * rgb[0] + 0.3575761 * rgb[1] + 0.1804375 * rgb[2]) / 0.95047;
    const double y = 0.2126729 * rgb[0] + 0.7151522 * rgb[1] + 0.0721750 * rgb[2];
    const double z = (0.0193339 * rgb[0] + 0.1191920 * rgb[1] + 0.9503041 * rgb[2]) / 1.08883;
    const auto f = [](double v) {
        return v > 216.0 / 24389 ? std::cbrt(v) : v * (24389.0 / 27) / 116 + 16.0 / 116;
    };
    return {116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))};
}
}
int main() {
    using namespace prism;
    auto device = backends::MetalDevice::create();
    if (!device) {
        return 77;
    }
    auto &metal = static_cast<backends::MetalDevice &>(*device);
    std::vector<float> samples(8 * 512);
    for (std::size_t m = 0; m < 8; ++m) {
        for (std::size_t n = 0; n < 512; ++n) {
            const double p = math::wavetableTurn * static_cast<double>(n) / 512;
            samples[m * 512 + n] = static_cast<float>(
                0.5 * std::sin(p) + 0.25 * std::sin(3 * p + m * 0.3) + 0.15 * std::sin(7 * p));
        }
    }
    widgets::GpuWavetable3d wave;
    if (!wave.build({samples, 8, 512})) {
        return 1;
    }
    render::GradientTable gradients;
    std::array<float, 1024> rgba{};
    const auto palette = math::thermalPalette();
    for (std::size_t i = 0; i < palette.size(); ++i) {
        rgba[4 * i] = static_cast<float>(palette[i].r);
        rgba[4 * i + 1] = static_cast<float>(palette[i].g);
        rgba[4 * i + 2] = static_cast<float>(palette[i].b);
        rgba[4 * i + 3] = 1;
    }
    const auto row = gradients.addHdr(rgba);
    constexpr hal::Extent extent{320, 180};
    backends::CpuRefSurface cpu({extent});
    auto gpu = metal.createRenderSurface({extent});
    const std::array<render::TextureView, 1> textures{wave.field()};
    for (int scenario = 0; scenario < 4; ++scenario) {
        widgets::Wavetable3dSettings settings;
        settings.phases = scenario == 1 ? 128 : 256;
        settings.position = 0.19f + scenario * 0.21f;
        settings.warp.quantization = 8;
        settings.morph3d = scenario == 1 ? 0 : scenario == 2 ? 0.5f : 1;
        if (scenario == 3) {
            settings.camera.yaw = 2.2;
            settings.camera.pitch = 1;
            settings.camera.inverseDistance = 0.125;
            settings.voices = 3;
            settings.detune = 0.3f;
            settings.time = 1;
        }
        render::DrawList list;
        list.roundedRect({0, 0, 320, 180}, {}, render::packColor(11, 12, 14, 255));
        wave.draw(list, {0, 0, 320, 180}, 0, row, settings);
        list.roundedRect({4, 4, 12, 12}, {}, render::packColor(90, 110, 130, 255));
        const auto view = test::viewOf(list, textures, gradients.rows());
        const render::BloomSettings bloom{0.85f, 0.25f, 0.25f, 3, true};
        const hal::DamageRegion damage({0, 0, 320, 180});
        const auto render = [&](render::RenderSurface &surface) {
            std::vector<float> pixels(static_cast<std::size_t>(extent.width) * extent.height * 4);
            if (!surface.render({view, &damage, {0, 0, 0, 1}, &bloom}) ||
                !surface.readback(pixels)) {
                pixels.clear();
            }
            return pixels;
        };
        const auto expected = render(cpu), actual = render(*gpu);
        if (expected.empty() || actual.empty()) {
            return 1;
        }
        std::vector<test::Pixel> a(actual.size() / 4), b(a.size());
        double worst = 0;
        std::size_t worstPixel = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            for (std::size_t c = 0; c < 4; ++c) {
                a[i][c] = actual[4 * i + c];
                b[i][c] = expected[4 * i + c];
            }
            const double delta =
                test::deltaE2000(lab(actual.data() + 4 * i), lab(expected.data() + 4 * i));
            if (delta > worst) {
                worst = delta;
                worstPixel = i;
            }
        }
        const double psnr = test::comparePixels(a, b).psnr;
        std::cout << "Manifold " << scenario << ": " << psnr << " dB, deltaE " << worst << " at "
                  << worstPixel % extent.width << ',' << worstPixel / extent.width << ", GPU "
                  << static_cast<backends::MetalSurface &>(*gpu).lastGpuMilliseconds() << " ms\n";
        if (psnr < 52 || worst > 0.5) {
            return 1;
        }
    }
}
