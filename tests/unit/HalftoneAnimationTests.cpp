#include "../support/AllocGuard.h"
#include "../support/ReferenceScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "math/ThermalPalette.h"
#include "render/BlueNoise.h"
#include "render/GradientTable.h"

#include <array>
#include <cmath>
#include <iostream>

int main() {
    using namespace prism;
    render::BlueNoise noise;
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
    std::array<float, 64 * 16> field{};
    const std::array<render::TextureView, 2> textures{render::TextureView{64, 16, 1, field},
                                                      noise.texture()};
    render::HalftonePayload h{};
    h.rect[2] = 32;
    h.rect[3] = 16;
    h.basis[0] = 4;
    h.basis[3] = 4;
    h.gutter[0] = 0.8f;
    h.gutter[1] = 1.2f;
    h.spotParams[0] = 0.6f;
    h.spotParams[1] = 2;
    h.spotParams[2] = 0.52f;
    h.spotParams[3] = 0.75f;
    h.screenParams[0] = 0.85f;
    h.screenParams[1] = 0.38f;
    h.screenParams[2] = 0.18f;
    h.paletteRow = row;
    h.blueNoiseSlot = 1;
    render::DrawList list;
    backends::CpuRefSurface surface({{32, 16}});
    surface.reserveFrame(1, 2);
    const hal::DamageRegion damage({0, 0, 32, 16});
    std::array<float, 32 * 16 * 4> pixels{}, previous{};
    const auto frame = [&](double time) {
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 64; ++x) {
                const double center = 0.4 + 0.06 * std::sin(time);
                field[static_cast<std::size_t>(y * 64 + x)] = static_cast<float>(
                    std::exp(-std::pow((y / 15.0 - center - 0.1 * std::sin(x * 0.08)) / 0.2, 2)));
            }
        }
        list.clear();
        list.halftoneField(h, 0);
        return surface.render(
                   {test::viewOf(list, textures, gradients.rows()), &damage, {0, 0, 0, 1}}) &&
               surface.readback(pixels);
    };
    if (!frame(0)) {
        return 1;
    }
    for (int rate : {60, 120, 240}) {
        if (!frame(0)) {
            return 1;
        }
        previous = pixels;
        double maximum = 0;
        for (int i = 1; i <= rate; ++i) {
            if (!frame(static_cast<double>(i) / rate)) {
                return 1;
            }
            for (std::size_t p = 0; p < pixels.size(); ++p) {
                maximum = std::max(maximum, std::abs(static_cast<double>(pixels[p] - previous[p])));
            }
            previous = pixels;
        }
        std::cout << rate << " Hz maximum linear frame delta " << maximum << '\n';
        if (maximum > 0.08) {
            return 1;
        }
    }
    {
        test::AllocGuard guard;
        for (int i = 0; i < 10000; ++i) {
            if (!frame(i / 240.0)) {
                return 1;
            }
        }
    }
    std::cout << "10,000 animated halftone renders without allocation\n";
}
