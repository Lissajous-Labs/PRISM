#include "../support/ReferenceScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "math/ThermalPalette.h"
#include "render/GradientTable.h"

#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <vector>

int main() {
    using namespace prism;
    render::GradientTable gradients;
    std::array<float, 1024> rgba{};
    for (std::size_t i = 0; i < 256; ++i) {
        rgba[4 * i] = rgba[4 * i + 1] = rgba[4 * i + 2] = 1;
        rgba[4 * i + 3] = 1;
    }
    const auto row = gradients.addHdr(rgba);
    const std::array<float, 1> field{1};
    const std::array<render::TextureView, 1> textures{render::TextureView{1, 1, 1, field}};
    bool ok = true;
    for (double period : {1.2, 1.6, 2.1, 2.7, 4.0, 8.0, 16.0}) {
        const auto width = static_cast<std::uint32_t>(std::round(period * 200));
        const auto height = static_cast<std::uint32_t>(std::round(period * 40));
        backends::CpuRefSurface surface({{width, height}});
        render::HalftonePayload h{};
        h.rect[2] = static_cast<float>(width);
        h.rect[3] = height;
        h.basis[0] = h.basis[3] = static_cast<float>(period);
        h.gutter[0] = h.gutter[1] = static_cast<float>(period * 0.3);
        h.spotParams[0] = static_cast<float>(period * 0.2);
        h.spotParams[1] = 2;
        h.spotParams[2] = 1;
        h.spotParams[3] = 1;
        h.screenParams[2] = 0.18f;
        h.blueNoiseSlot = render::noTexture;
        h.paletteRow = row;
        render::DrawList list;
        list.halftoneField(h, 0);
        const auto pixels =
            test::renderFull(surface, test::viewOf(list, textures, gradients.rows()));
        std::vector<double> trace(width);
        double mean = 0;
        for (std::uint32_t x = 0; x < width; ++x) {
            for (std::uint32_t y = 0; y < height; ++y) {
                trace[x] += pixels[(static_cast<std::size_t>(y) * width + x) * 4] / height;
            }
            mean += trace[x] / width;
        }
        const double fraction =
            math::pillArea(period * 0.7, period * 0.7, period * 0.2) / (period * period);
        const double expected = 0.0033465358 + fraction * (1 - 0.0033465358);
        const double areaError = std::abs(mean - expected);
        std::cout << "Period " << period << " macro error " << areaError;
        if (period <= 2.7) {
            double strongest = 0;
            for (std::uint32_t k = 1; k < width / 4; ++k) {
                std::complex<double> sum{};
                for (std::uint32_t x = 0; x < width; ++x) {
                    const double angle = -2 * std::numbers::pi * k * x / width;
                    sum +=
                        (trace[x] - mean) * std::complex<double>(std::cos(angle), std::sin(angle));
                }
                strongest = std::max(strongest, std::abs(sum) / (width * mean));
            }
            const double beatDb = 20 * std::log10(std::max(strongest, 1e-16));
            std::cout << ", low-frequency beat " << beatDb << " dB";
            ok = ok && beatDb <= -50;
        }
        std::cout << '\n';
        ok = ok && areaError <= 0.002;
    }
    return ok ? 0 : 1;
}
