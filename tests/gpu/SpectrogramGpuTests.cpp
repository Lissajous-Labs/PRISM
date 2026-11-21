#include "../support/AllocGuard.h"
#include "../support/PixelOracle.h"
#include "../support/ReferenceScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "backends/metal/MetalBackend.h"
#include "widgets/GpuHalftoneSpectrogram.h"

#include <array>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace prism;
    auto device = backends::MetalDevice::create();
    if (!device) {
        return 77;
    }
    auto &metal = static_cast<backends::MetalDevice &>(*device);
    constexpr std::uint32_t frames = 256, period = 2048, rows = 64;
    std::vector<float> samples(static_cast<std::size_t>(frames) * period);
    for (std::uint32_t m = 0; m < frames; ++m) {
        for (std::uint32_t n = 0; n < period; ++n) {
            const double phase = math::wavetableTurn * n / period;
            const double morph = static_cast<double>(m) / (frames - 1);
            samples[static_cast<std::size_t>(m) * period + n] = static_cast<float>(
                0.35 * std::sin(phase) + 0.22 * std::sin(7 * phase + morph) +
                (0.1 + 0.1 * morph) * std::sin(19 * phase) + 0.08 * std::sin(67 * phase));
        }
    }
    const math::WavetableView table{samples, frames, period};
    widgets::GpuHalftoneSpectrogram reference(frames, period, rows);
    auto native = metal.createSpectrogram(table, rows);
    backends::CpuRefSurface cpu({{frames, rows}});
    auto gpu = metal.createRenderSurface({{frames, rows}});
    math::WavetableWarp warp;
    warp.mirror = 0.35;
    warp.bend = 0.2;
    warp.quantization = 8;
    warp.amplitudeModulation = 0.2;
    for (int scenario = 0; scenario < 2; ++scenario) {
        const math::WavetableWarp selected = scenario == 0 ? math::WavetableWarp{} : warp;
        if (!reference.build(table, selected) || !native->prepare(selected) ||
            !native->dispatch()) {
            return 1;
        }
        render::DrawList list;
        list.texturedQuad(render::PrimitiveKind::texturedQuad, {0, 0, frames, rows}, {0, 0, 1, 1},
                          0, 0xFFFFFFFFu);
        const std::array<render::TextureView, 1> cpuTextures{reference.field()},
            gpuTextures{native->field()};
        const auto expected = test::renderFull(cpu, test::viewOf(list, cpuTextures));
        const auto actual = test::renderFull(*gpu, test::viewOf(list, gpuTextures));
        if (expected.empty() || actual.empty()) {
            return 1;
        }
        double maximum = 0;
        for (std::size_t i = 0; i < actual.size(); i += 4) {
            maximum = std::max(maximum, std::abs(static_cast<double>(actual[i] - expected[i])));
        }
        std::cout << "GPU FFT " << scenario << " maximum field error " << maximum
                  << ", total compute " << native->lastGpuMilliseconds() << " ms\n";
        if (maximum > 0.002) {
            return 1;
        }
    }
    for (int i = 0; i < 100; ++i) {
        {
            test::AllocGuard guard;
            if (!native->prepare(warp, 0.25f, static_cast<std::uint32_t>(i), 1)) {
                return 1;
            }
        }
        if (!native->dispatch()) {
            return 1;
        }
    }
    const hal::DamageRegion damage({0, 0, frames, rows});
    render::DrawList list;
    list.texturedQuad(render::PrimitiveKind::texturedQuad, {0, 0, frames, rows}, {0, 0, 1, 1}, 0,
                      0xFFFFFFFFu);
    const std::array<render::TextureView, 1> gpuTextures{native->field()};
    if (cpu.render({test::viewOf(list, gpuTextures), &damage, {0, 0, 0, 1}})) {
        std::cerr << "CPU accepted a native-only texture\n";
        return 1;
    }
}
