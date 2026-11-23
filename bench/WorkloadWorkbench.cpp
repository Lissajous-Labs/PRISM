#include "BenchSupport.h"
#include "BenchText.h"
#include "Workloads.h"

#include "render/GradientTable.h"
#include "telemetry/LoudnessMeter.h"
#include "widgets/GpuLoudnessMeter.h"
#include "widgets/GpuScope.h"
#include "widgets/GpuSpectrum.h"
#include "widgets/NodeCard.h"
#include "widgets/RotaryKnob.h"
#include "widgets/VerletCableLayer.h"

#include <array>
#include <cmath>
#include <deque>
#include <numbers>
#include <string>
#include <vector>

namespace prism::bench {
namespace {
struct QuietSink final : host::HostParameterSink {
    void beginGesture(std::uint32_t) override {}
    void setValueNotifyingHost(std::uint32_t, float) override {}
    void endGesture(std::uint32_t) override {}
};

std::vector<host::ParameterSpec> knobSpecs() {
    std::vector<host::ParameterSpec> specs;
    for (int i = 0; i < 128; ++i) {
        specs.push_back({"p" + std::to_string(i), "P" + std::to_string(i), 0, 1, 0.5f, 0, ""});
    }
    return specs;
}

struct Workbench {
    host::ParameterMirror mirror{knobSpecs()};
    QuietSink sink;
    std::vector<widgets::NodeCard> cards;
    std::vector<widgets::RotaryKnob> knobs;
    widgets::VerletCableLayer cables{64};
    std::vector<widgets::GpuScope> scopes;
    std::vector<widgets::GpuSpectrum> spectra;
    std::deque<telemetry::LoudnessMeter> meters;
    std::vector<text::TextLayout> labels;
    text::TextLayout titleLayout;
    std::vector<float> block = std::vector<float>(4096);
    BenchText text;
    render::GradientTable gradients;
    std::uint32_t row = 0;

    explicit Workbench(BenchText loaded) : text(std::move(loaded)) {
        titleLayout = text.shaper->shape({"Operator", 11});
        const std::array<widgets::Port, 4> ports{
            {{{150, 40}, true}, {{150, 80}, true}, {{0, 40}, false}, {{0, 80}, false}}};
        cards.reserve(16);
        for (int i = 0; i < 16; ++i) {
            cards.emplace_back(math::Vector2{40.0 + 300 * (i % 4), 40.0 + 170 * (i / 4)},
                               math::Vector2{150, 130}, ports, "Operator");
        }
        for (int i = 0; i < 64; ++i) {
            const auto &from = cards[static_cast<std::size_t>(i % 16)];
            const auto &to = cards[static_cast<std::size_t>((i * 5 + 3) % 16)];
            static_cast<void>(cables.connect(from, static_cast<std::size_t>(i % 2), to,
                                             static_cast<std::size_t>(2 + (i / 2) % 2)));
        }
        knobs.reserve(128);
        for (int i = 0; i < 128; ++i) {
            const auto &card = cards[static_cast<std::size_t>(i % 16)];
            const int slot = i / 16;
            knobs.emplace_back(widgets::BoundParameter{mirror, sink, static_cast<std::uint32_t>(i)},
                               card.position() +
                                   math::Vector2{22.0 + 36 * (slot % 4), 50.0 + 40 * (slot / 4)},
                               13);
        }
        for (int i = 0; i < 8; ++i) {
            scopes.emplace_back(256, 1 << 15);
            meters.emplace_back(48000);
        }
        for (int i = 0; i < 4; ++i) {
            spectra.emplace_back(256, 48000);
        }
        for (int i = 0; i < 300; ++i) {
            labels.push_back(text.shaper->shape({"Label " + std::to_string(i), 11}));
        }
        const std::array<render::GradientStop, 2> stops{
            render::GradientStop{0, render::packColor(255, 120, 40, 230)},
            render::GradientStop{1, render::packColor(40, 60, 255, 40)}};
        row = gradients.add(stops, render::GradientSpace::oklab);
    }

    void telemetry(int frame) {
        for (std::size_t n = 0; n < block.size(); ++n) {
            const double t = (static_cast<double>(frame) * 400 + static_cast<double>(n)) / 48000;
            block[n] = static_cast<float>(0.5 * std::sin(2 * std::numbers::pi * 220 * t) +
                                          0.2 * std::sin(2 * std::numbers::pi * 3100 * t));
        }
        for (std::size_t i = 0; i < scopes.size(); ++i) {
            scopes[i].append(std::span(block).first(400));
            scopes[i].build(8);
            meters[i].process(block.data(), block.data(), 400);
        }
        if (frame % 4 == 0) {
            for (auto &spectrum : spectra) {
                spectrum.analyze(block, 1.0 / 30);
                spectrum.buildStrip(-90, 0);
            }
        }
    }

    void record(render::DrawList &list, int frame) {
        list.clear();
        text.engine->beginFrame();
        const auto transform =
            *list.addTransform(math::Affine2D::scale(displayScale, displayScale));
        list.setTransform(transform);
        list.roundedRect({0, 0, 1280, 720}, {}, render::packColor(22, 24, 28, 255));
        const auto title = [&](render::DrawList &out, std::string_view, math::Vector2 at,
                               std::uint32_t color) {
            text.engine->draw(out, titleLayout, at, {color}, 11, displayScale);
        };
        const widgets::WidgetColors colors;
        for (const auto &card : cards) {
            card.paint(list, colors, title);
        }
        mirror.hostSetNormalized(static_cast<std::uint32_t>(frame % 128),
                                 0.5f + 0.5f * std::sin(static_cast<float>(frame) * 0.1f));
        for (const auto &knob : knobs) {
            knob.paint(list, colors);
        }
        cables.paint(list, render::packColor(240, 140, 80, 255), 2);
        for (std::size_t i = 0; i < scopes.size(); ++i) {
            const float x = 1240.0f - 180;
            const float y = 20.0f + 40 * static_cast<float>(i);
            scopes[i].draw(list, {x, y, x + 170, y + 34}, static_cast<std::uint32_t>(3 + i), 1.2f,
                           render::packColor(80, 255, 160, 255));
            widgets::drawLoudnessMeter(list, {x - 14, y, x - 6, y + 34}, meters[i].reading(), -1,
                                       {-60, 0, 6, render::packColor(30, 30, 36, 255),
                                        render::packColor(60, 220, 120, 255),
                                        render::packColor(200, 200, 200, 255),
                                        render::packColor(255, 80, 60, 255)});
        }
        for (std::size_t i = 0; i < spectra.size(); ++i) {
            const float x = 1060.0f, y = 360.0f + 80 * static_cast<float>(i);
            spectra[i].draw(list, {x, y, x + 200, y + 70}, render::packColor(255, 255, 255, 255));
        }
        for (std::size_t i = 0; i < labels.size(); ++i) {
            const float x = 20.0f + 64 * static_cast<float>(i % 15);
            const float y = 700.0f - 12 * static_cast<float>(i / 15) * 0.35f;
            text.engine->draw(list, labels[i], {x, y}, {render::packColor(200, 200, 200, 255)}, 11,
                              displayScale);
        }
    }

    void textures(std::vector<render::TextureView> &views) const {
        views.clear();
        for (const auto &view : text.engine->textures()) {
            views.push_back(view);
        }
        for (const auto &scope : scopes) {
            views.push_back(scope.strip());
        }
        for (const auto &spectrum : spectra) {
            views.push_back(spectrum.strip());
        }
    }
};
}

int runWorkbench() {
    auto context = openContext();
    auto text = loadBenchText(0);
    if (!context || !text) {
        return skipCode;
    }
    auto bench = std::make_unique<Workbench>(std::move(*text));
    render::DrawList list;
    const hal::DamageRegion damage({0, 0, static_cast<std::int32_t>(benchExtent.width),
                                    static_cast<std::int32_t>(benchExtent.height)});
    constexpr int warmupFrames = 60;
    constexpr int measuredFrames = 10000;
    telemetry::FrameStatistics host(measuredFrames), gpu(measuredFrames), frameTime(measuredFrames);
    std::vector<render::TextureView> textures;
    std::size_t allocations = 0, systemAllocations = 0;
    for (int frame = 0; frame < warmupFrames + measuredFrames; ++frame) {
        bench->telemetry(frame);
        const bool measured = frame >= warmupFrames;
        countAllocations(measured);
        const Stopwatch encode;
        bench->cables.step(1.0 / 120);
        bench->record(list, frame);
        bench->textures(textures);
        const render::FrameView view{list.primitives(), list.payload(),          list.transforms(),
                                     list.clips(),      bench->gradients.rows(), textures,
                                     list.paths(),      list.pathSegments()};
        if (!context->surface->render({view, &damage, {0, 0, 0, 1}}) ||
            !context->metal().lastSubmitted()) {
            countAllocations(false);
            return 1;
        }
        const double encodeMs = encode.milliseconds();
        allocations += measured ? allocationCount() : 0;
        systemAllocations += measured ? systemAllocationCount() : 0;
        countAllocations(false);
        context->surface->waitIdle();
        if (measured) {
            host.add(encodeMs);
            gpu.add(context->metal().lastGpuMilliseconds());
            frameTime.add(encode.milliseconds());
        }
    }
    Report report("W2");
    report.distribution("host encode ms", host);
    report.distribution("GPU ms", gpu);
    report.distribution("offscreen completed frame ms", frameTime);
    report.below("GPU Q99 ms", gpu.quantile(0.99), 2.5);
    report.below("host encode Q99 ms", host.quantile(0.99), 0.6);
    report.below("completed frame Q99 ms for 120 Hz budget", frameTime.quantile(0.99),
                 1000.0 / 120);
    report.value("Metal and system framework heap allocations across 10000 frames",
                 static_cast<double>(systemAllocations));
    report.below("PRISM heap allocations across 10000 frames", static_cast<double>(allocations), 1);
    return report.finish();
}
}
