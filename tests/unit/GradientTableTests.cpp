#include "../support/AllocGuard.h"
#include "../support/GradientScene.h"
#include "../support/ReferenceScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "math/ColorSpace.h"
#include "render/GradientParameter.h"

#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
using namespace prism;

math::LinearRgb straight(std::uint32_t packed) {
    const auto channel = [&](int shift) {
        return math::srgbDecode(static_cast<double>((packed >> shift) & 0xFFu) / 255);
    };
    return {channel(0), channel(8), channel(16)};
}

bool checkTable() {
    render::GradientTable table;
    const std::array<render::GradientStop, 2> stops{
        render::GradientStop{0, render::packColor(255, 0, 0, 255)},
        render::GradientStop{1, render::packColor(0, 0, 255, 128)}};
    const auto row = table.add(stops, render::GradientSpace::oklab);
    const auto again = table.add(stops, render::GradientSpace::oklab);
    const auto other = table.add(stops, render::GradientSpace::oklch);
    const auto texels =
        table.rows().subspan(row * render::gradientRowTexels * 4, render::gradientRowTexels * 4);
    const double t = 51.0 / 255.0;
    const auto expectedRgb = math::fromOklab(math::mixOklab(
        math::toOklab(straight(stops[0].color)), math::toOklab(straight(stops[1].color)), t));
    const double alpha = 1 + (128.0 / 255.0 - 1) * t;
    const auto close = [](double a, double b) { return std::abs(a - b) < 1e-6; };
    return row == again && other != row && table.rowCount() == 2 && close(texels[0], 1) &&
           close(texels[2], 0) && close(texels[255 * 4 + 2], 128.0 / 255.0) &&
           close(texels[255 * 4 + 3], 128.0 / 255.0) &&
           close(texels[51 * 4], expectedRgb.r * alpha) &&
           close(texels[51 * 4 + 2], expectedRgb.b * alpha) && close(texels[51 * 4 + 3], alpha);
}

bool checkParameters() {
    const math::Vector2 center{0, 0};
    const double concentric = render::radialParameter(center, center, 10, {6, 0});
    const double onEdge = render::radialParameter(center, {10, 0}, 10, {5, 3});
    const double outside = render::radialParameter(center, {3, 0}, 10, {-10, 0});
    const float conic[6] = {0, 0, 0, 0, 0, 0};
    const double quarter = render::gradientParameter(render::GradientShape::conic, conic, {0, 1});
    const double wrapped =
        render::gradientParameter(render::GradientShape::conic, conic, {1, -1e-9});
    return std::abs(concentric - 0.6) < 1e-12 && std::isfinite(onEdge) && onEdge > 0 &&
           std::abs(outside - 1) < 1e-12 && std::abs(quarter - 0.25) < 1e-12 && wrapped > 0.99;
}

bool checkDarkRampBanding() {
    constexpr std::uint32_t width = 256;
    test::GradientScene scene(width);
    backends::CpuRefSurface surface({{width, 128}});
    const auto view = test::viewOf(scene.list, {}, scene.table.rows());
    const auto linear = test::renderFull(surface, view);
    std::vector<std::uint8_t> presented(linear.size());
    if (linear.empty() || !surface.readbackPresented(presented, 3)) {
        return false;
    }
    double dithered = 0;
    double banded = 0;
    for (std::uint32_t block = width / 8 / 4; block < width / 8; ++block) {
        double ditheredError = 0;
        double bandedError = 0;
        for (std::uint32_t x = block * 8; x < block * 8 + 8; ++x) {
            const double target = 255 * math::srgbEncode(linear[static_cast<std::size_t>(x) * 4]);
            for (std::uint32_t y = 0; y < 64; ++y) {
                ditheredError += presented[(static_cast<std::size_t>(y) * width + x) * 4] - target;
                bandedError += std::floor(target + 0.5) - target;
            }
        }
        dithered = std::max(dithered, std::abs(ditheredError) / 512);
        banded = std::max(banded, std::abs(bandedError) / 512);
    }
    std::cout << "Dark ramp block error: dithered " << dithered << " LSB, undithered " << banded
              << " LSB\n";
    return dithered < 0.1 && banded > 0.2;
}
}

int main() {
    if (!checkTable() || !checkParameters() || !checkDarkRampBanding()) {
        std::cerr << "Gradient contract failed\n";
        return 1;
    }
    return 0;
}
