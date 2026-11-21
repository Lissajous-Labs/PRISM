#include "BenchSupport.h"
#include "Workloads.h"

#include "render/DrawList.h"
#include "render/effects/Bloom.h"

#include <cmath>
#include <vector>

namespace prism::bench {
namespace {
constexpr hal::Extent ultra{3840, 2160};

void buildScene(render::DrawList &list, int frame) {
    list.clear();
    list.roundedRect({0, 0, 3840, 2160}, {}, render::packColor(18, 20, 26, 255));
    for (int i = 0; i < 48; ++i) {
        const float x = 120.0f + 300 * static_cast<float>(i % 12);
        const float y = 200.0f + 420 * static_cast<float>(i / 12);
        list.roundedRect({x, y, x + 220, y + 140}, {18, 18, 18, 18},
                         render::packColor(static_cast<std::uint8_t>(60 + 4 * i), 140,
                                           static_cast<std::uint8_t>(240 - 4 * i), 255));
    }
    for (int i = 0; i < 64; ++i) {
        const float x = 200.0f + 55 * static_cast<float>(i) + static_cast<float>(frame % 2);
        const float y = 1900.0f;
        list.roundedRect({x, y, x + 1, y + 1}, {}, render::packColor(255, 255, 255, 255));
    }
    list.backdropBlur({400, 300, 3440, 1860}, {40, 40, 40, 40}, 32);
}
}

int runEffects() {
    auto context = openContext(ultra);
    if (!context) {
        return skipCode;
    }
    render::DrawList list;
    const hal::DamageRegion damage({0, 0, 3840, 2160});
    const render::BloomSettings bloom{0.9f, 0.4f, 0.6f, 5, true};
    telemetry::FrameStatistics gpu(256);
    std::vector<float> pixels(static_cast<std::size_t>(ultra.width) * ultra.height * 4);
    std::vector<double> energies;
    for (int frame = 0; frame < 120; ++frame) {
        buildScene(list, frame);
        const render::FrameView view{
            list.primitives(), list.payload(),      list.transforms(), list.clips(), {}, {},
            list.paths(),      list.pathSegments(), list.effects()};
        if (!context->surface->render({view, &damage, {0, 0, 0, 1}, &bloom})) {
            return 1;
        }
        if (frame >= 10) {
            gpu.add(context->metal().lastGpuMilliseconds());
        }
        if (frame >= 100 && frame < 108 && context->surface->readback(pixels)) {
            double energy = 0;
            for (std::size_t y = 1700; y < 2100; ++y) {
                for (std::size_t x = 0; x < ultra.width; ++x) {
                    const std::size_t i = (y * ultra.width + x) * 4;
                    energy += 0.2126 * pixels[i] + 0.7152 * pixels[i + 1] + 0.0722 * pixels[i + 2];
                }
            }
            energies.push_back(energy);
        }
    }
    double low = energies.front(), high = energies.front();
    for (const double energy : energies) {
        low = std::min(low, energy);
        high = std::max(high, energy);
    }
    Report report("W5");
    report.distribution("GPU ms", gpu);
    report.below("bloom shimmer under one-pixel motion", (high - low) / high, 0.01);
    report.below("GPU Q50 ms at 3840x2160", gpu.quantile(0.5), 3.0);
    return report.finish();
}
}
