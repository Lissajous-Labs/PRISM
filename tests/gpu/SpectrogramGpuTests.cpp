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

namespace {
bool checkRealFftSizes(prism::backends::MetalDevice &metal) {
    using namespace prism;
    constexpr std::array<std::uint32_t, 9> periods{8, 16, 32, 64, 128, 256, 512, 1024, 2048};
    constexpr std::uint32_t frames = 5, rows = 13;
    for (const auto period : periods) {
        std::vector<float> samples(static_cast<std::size_t>(frames) * period);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            for (std::uint32_t n = 0; n < period; ++n) {
                const double phase = math::wavetableTurn * n / period;
                samples[static_cast<std::size_t>(frame) * period + n] =
                    static_cast<float>(0.1 + 0.3 * std::sin(phase + frame * 0.2) +
                                       0.2 * std::cos(3 * phase) + (n % 2 == 0 ? 0.05 : -0.05));
            }
        }
        const math::WavetableView table{samples, frames, period};
        widgets::GpuHalftoneSpectrogram reference(frames, period, rows);
        auto native = metal.createSpectrogram(table, rows);
        backends::CpuRefSurface cpu({{frames, rows}});
        auto gpu = metal.createRenderSurface({{frames, rows}});
        render::DrawList list;
        list.texturedQuad(render::PrimitiveKind::texturedQuad, {0, 0, frames, rows}, {0, 0, 1, 1},
                          0, 0xFFFFFFFFu);
        for (int scenario = 0; scenario < 2; ++scenario) {
            math::WavetableWarp warp;
            if (scenario != 0) {
                warp.mirror = 0.35;
                warp.bend = -0.2;
                warp.sync = 1.3;
                warp.phaseOffset = -0.3;
            }
            if (not reference.build(table, warp) or not native->prepare(warp) or
                not native->dispatch()) {
                return false;
            }
            const std::array<render::TextureView, 1> cpuTextures{reference.field()},
                gpuTextures{native->field()};
            const auto expected = test::renderFull(cpu, test::viewOf(list, cpuTextures));
            const auto actual = test::renderFull(*gpu, test::viewOf(list, gpuTextures));
            if (expected.empty() or actual.empty()) {
                return false;
            }
            for (std::size_t i = 0; i < actual.size(); i += 4) {
                if (not std::isfinite(actual[i]) or std::abs(actual[i] - expected[i]) > 0.002) {
                    std::cerr << "Packed real FFT mismatch at period " << period << ", scenario "
                              << scenario << ", pixel " << i / 4 << '\n';
                    return false;
                }
            }
        }
    }
    std::cout << "Packed real FFT CPU parity passed for periods 8 through 2048\n";
    return true;
}
}

int main() {
    using namespace prism;
    auto device = backends::MetalDevice::create();
    if (!device) {
        return 77;
    }
    auto &metal = static_cast<backends::MetalDevice &>(*device);
    if (not checkRealFftSizes(metal)) {
        return 1;
    }
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
    for (int scenario = 0; scenario < 4; ++scenario) {
        const math::WavetableWarp selected = scenario % 2 == 0 ? math::WavetableWarp{} : warp;
        if (scenario == 2) {
            native = metal.createSpectrogram(table, rows);
        }
        if (scenario >= 2 && !native->prepare(selected, 0.25f, 3, 1)) {
            return 1;
        }
        const bool prepared =
            scenario < 2 ? native->prepare(selected) : native->prepare(selected, 0.25f, 7, 1);
        if (!reference.build(table, selected) || !prepared ||
            (scenario < 2 && !native->dispatch())) {
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
    double fastest = 1e9;
    for (int i = 0; i < 50; ++i) {
        if (!native->prepare(warp) || !native->dispatch()) {
            return 1;
        }
        fastest = std::min(fastest, native->lastGpuMilliseconds());
    }
    std::cout << "Full 256-frame rebuild " << fastest << " ms\n";
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
