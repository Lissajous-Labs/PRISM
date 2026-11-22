#include "../support/ReferenceScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "backends/metal/MetalBackend.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
using namespace prism;

void scene(render::DrawList &list, unsigned frame) {
    list.clear();
    const auto red = static_cast<std::uint8_t>((frame * 29) % 256);
    const auto green = static_cast<std::uint8_t>((frame * 53) % 256);
    list.roundedRect({0, 0, 128, 128}, {}, render::packColor(red, green, 71, 255));
    render::VectorPath path;
    const double x = static_cast<double>(frame % 64);
    path.moveTo({x, 10.25});
    path.lineTo({x + 40.5, 17.75});
    path.lineTo({x + 20.25, 109.5});
    path.close();
    list.fillPath(path, render::packColor(31, 181, 219, 255));
}

bool compare(render::RenderSurface &gpu, backends::CpuRefSurface &cpu, render::DrawList &list,
             unsigned frame) {
    scene(list, frame);
    const auto expected = test::renderFull(cpu, test::viewOf(list));
    std::vector<float> actual(expected.size());
    if (expected.empty() or not gpu.readback(actual)) {
        return false;
    }

    double worst = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        worst = std::max(worst, static_cast<double>(std::abs(expected[i] - actual[i])));
    }
    if (worst >= 1e-3) {
        std::cerr << "Asynchronous frame mismatch: " << worst << " at frame " << frame << '\n';
        return false;
    }
    return true;
}
}

int main() {
    auto created = backends::MetalDevice::create();
    if (not created) {
        std::cout << "No native GPU device available\n";
        return 77;
    }

    auto &device = static_cast<backends::MetalDevice &>(*created);
    auto surface = device.createRenderSurface({{128, 128}});
    auto &gpu = static_cast<backends::MetalSurface &>(*surface);
    backends::CpuRefSurface cpu({{128, 128}});
    render::DrawList list;
    hal::DamageRegion full({0, 0, 128, 128});
    hal::DamageRegion partial({0, 0, 16, 16});
    unsigned submitted = 0;
    unsigned lastFrame = 0;
    for (unsigned frame = 0; frame < 200; ++frame) {
        scene(list, frame);
        if (not gpu.render({test::viewOf(list), &full, {0, 0, 0, 1}})) {
            return 1;
        }
        if (gpu.lastSubmitted()) {
            lastFrame = frame;
            ++submitted;
        }
    }
    gpu.waitIdle();
    if (submitted < 3 or not compare(gpu, cpu, list, lastFrame)) {
        return 1;
    }

    scene(list, 201);
    if (not gpu.render({test::viewOf(list), &full, {0, 0, 0, 1}})) {
        return 1;
    }
    gpu.resize({128, 128});
    scene(list, 202);
    if (not gpu.render({test::viewOf(list), &partial, {0, 0, 0, 1}}) or
        not compare(gpu, cpu, list, 202)) {
        return 1;
    }

    scene(list, 203);
    if (not gpu.render({test::viewOf(list), &full, {0, 0, 0, 1}})) {
        return 1;
    }
    gpu.loseDevice();
    if (not gpu.rebuild()) {
        return 1;
    }
    scene(list, 204);
    if (not gpu.render({test::viewOf(list), &partial, {0, 0, 0, 1}}) or
        not compare(gpu, cpu, list, 204)) {
        return 1;
    }

    std::cout << "Asynchronous upload ownership, resize and rebuild match CPU output; " << submitted
              << " burst frames submitted\n";
    return 0;
}
