#include "../support/PixelOracle.h"
#include "../support/ReferenceScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "backends/metal/MetalBackend.h"
#include "math/ThermalPalette.h"
#include "render/BlueNoise.h"
#include "render/GradientTable.h"

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
    auto &metalDevice = static_cast<backends::MetalDevice &>(*device);
    render::BlueNoise noise;
    std::array<int, 256> histogram{};
    for (float value : noise.texture().texels) {
        ++histogram[static_cast<std::size_t>(value * 255 + 0.5f)];
    }
    for (int count : histogram) {
        if (count != 16) {
            return 1;
        }
    }
    render::GradientTable gradients;
    std::array<float, 1024> rgba{};
    const auto palette = math::thermalPalette();
    for (std::size_t i = 0; i < 256; ++i) {
        rgba[4 * i] = static_cast<float>(palette[i].r);
        rgba[4 * i + 1] = static_cast<float>(palette[i].g);
        rgba[4 * i + 2] = static_cast<float>(palette[i].b);
        rgba[4 * i + 3] = 1;
    }
    const auto row = gradients.addHdr(rgba);
    std::array<float, 64 * 32> field{};
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 64; ++x) {
            field[static_cast<std::size_t>(y * 64 + x)] = static_cast<float>(
                0.15 +
                0.85 * std::exp(-std::pow((y / 31.0 - 0.5 - 0.3 * std::sin(x * 0.12)) / 0.12, 2)));
        }
    }
    std::array<float, 64 * 4> bands{};
    for (std::size_t i = 0; i < 64; ++i) {
        bands[4 * i] = static_cast<float>(0.4 + 0.35 * std::sin(i * 0.7));
        bands[4 * i + 1] = std::min(1.0f, bands[4 * i] + 0.13f);
        bands[4 * i + 2] = 0.35f;
        bands[4 * i + 3] = 1;
    }
    constexpr hal::Extent extent{320, 160};
    backends::CpuRefSurface cpu({extent});
    auto gpu = metalDevice.createRenderSurface({extent});
    for (int scenario = 0; scenario < 4; ++scenario) {
        const auto lattice = scenario == 2   ? math::HalftoneLattice::hexagonal(7)
                             : scenario == 3 ? math::HalftoneLattice::rotated(6.5, 0.4)
                                             : math::HalftoneLattice::brick(6, 4);
        render::HalftonePayload h{};
        h.rect[2] = 320;
        h.rect[3] = 160;
        h.basis[0] = static_cast<float>(lattice.b1.x);
        h.basis[1] = static_cast<float>(lattice.b1.y);
        h.basis[2] = static_cast<float>(lattice.b2.x);
        h.basis[3] = static_cast<float>(lattice.b2.y);
        h.gutter[0] = 1.5f;
        h.gutter[1] = 1.3f;
        h.spotParams[0] = 0.7f;
        h.spotParams[1] = 2;
        h.spotParams[2] = 0.52f;
        h.spotParams[3] = 0.75f;
        h.screenParams[0] = scenario == 1 ? 0 : 0.85f;
        h.screenParams[1] = 0.38f;
        h.screenParams[2] = 0.18f;
        h.cursorParams[0] = 0.43f;
        h.cursorParams[1] = 0.35f;
        h.cursorParams[2] = 0.035f;
        h.cursorParams[3] = 3;
        h.paletteRow = row;
        h.blueNoiseSlot = 1;
        h.modeFlags = scenario == 1 ? render::halftoneFlags::spectralMatrix
                      : scenario == 2
                          ? render::halftoneFlags::hexagonal | render::halftoneFlags::superellipse
                          : render::halftoneFlags::cursor;
        const render::TextureView fieldTexture{64, 32, 1, field};
        const render::TextureView bandTexture{64, 1, 4, bands};
        const std::array<render::TextureView, 2> textures{
            scenario == 1 ? bandTexture : fieldTexture, noise.texture()};
        render::DrawList list;
        list.halftoneField(h, 0);
        const render::FrameView view{list.primitives(), list.payload(),   list.transforms(),
                                     list.clips(),      gradients.rows(), textures};
        const render::BloomSettings bloom{0.85f, 0.25f, 0.25f, 3, true};
        const hal::DamageRegion damage({0, 0, 320, 160});
        const auto render = [&](render::RenderSurface &surface) {
            std::vector<float> pixels(static_cast<std::size_t>(extent.width) * extent.height * 4);
            if (!surface.render({view, &damage, {0, 0, 0, 1}, &bloom}) ||
                !surface.readback(pixels)) {
                pixels.clear();
            }
            return pixels;
        };
        const auto expected = render(cpu);
        const auto actual = render(*gpu);
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
        std::cout << "Halftone " << scenario << ": " << psnr << " dB, deltaE " << worst << '\n';
        if (psnr < 52 || worst > 0.5) {
            std::cerr << "Worst pixel " << worstPixel % extent.width << ','
                      << worstPixel / extent.width << " actual " << actual[4 * worstPixel] << ','
                      << actual[4 * worstPixel + 1] << ',' << actual[4 * worstPixel + 2]
                      << " expected " << expected[4 * worstPixel] << ','
                      << expected[4 * worstPixel + 1] << ',' << expected[4 * worstPixel + 2]
                      << '\n';
            return 1;
        }
    }
}
