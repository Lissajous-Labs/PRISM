#include "BenchSupport.h"
#include "Workloads.h"

#include "render/DrawList.h"
#include "widgets/GpuScope.h"

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace prism::bench {
namespace {
constexpr float scopeX = 900, scopeY = 40, scopeWidth = 300, scopeHeight = 60;

render::RectF meterRect(int meter, int channel) {
    const float x = 40.0f + 30 * static_cast<float>(meter) + 8 * static_cast<float>(channel);
    return {x, 500, x + 6, 660};
}

hal::Rect physical(render::RectF rect) {
    return {static_cast<std::int32_t>(std::floor(rect.x0 * displayScale)) - 2,
            static_cast<std::int32_t>(std::floor(rect.y0 * displayScale)) - 2,
            static_cast<std::int32_t>(std::ceil(rect.x1 * displayScale)) + 2,
            static_cast<std::int32_t>(std::ceil(rect.y1 * displayScale)) + 2};
}

void buildInterface(render::DrawList &list, const widgets::GpuScope &scope,
                    const std::array<float, 16> &levels) {
    list.clear();
    const auto transform = *list.addTransform(math::Affine2D::scale(displayScale, displayScale));
    list.setTransform(transform);
    list.roundedRect({0, 0, 1280, 720}, {}, render::packColor(24, 26, 30, 255));
    for (int i = 0; i < 400; ++i) {
        const float x = 20.0f + 30 * static_cast<float>(i % 40);
        const float y = 120.0f + 30 * static_cast<float>(i / 40) * 0.36f;
        list.roundedRect({x, y, x + 24, y + 8}, {3, 3, 3, 3},
                         render::packColor(static_cast<std::uint8_t>(70 + i % 90), 90, 110, 255));
    }
    for (int meter = 0; meter < 8; ++meter) {
        for (int channel = 0; channel < 2; ++channel) {
            const auto rect = meterRect(meter, channel);
            const float level = levels[static_cast<std::size_t>(meter * 2 + channel)];
            list.roundedRect(rect, {}, render::packColor(30, 30, 36, 255));
            list.roundedRect({rect.x0, rect.y1 - (rect.y1 - rect.y0) * level, rect.x1, rect.y1}, {},
                             render::packColor(60, 220, 120, 255));
        }
    }
    list.roundedRect({scopeX, scopeY, scopeX + scopeWidth, scopeY + scopeHeight}, {},
                     render::packColor(14, 16, 20, 255));
    scope.draw(list, {scopeX, scopeY, scopeX + scopeWidth, scopeY + scopeHeight}, 0, 1.2f,
               render::packColor(80, 255, 160, 255));
}
}

int runMetering() {
    auto context = openContext();
    auto reference = openContext();
    if (!context || !reference) {
        return skipCode;
    }
    widgets::GpuScope scope(600, 1 << 15);
    std::array<float, 16> levels{};
    std::vector<float> block(400);
    render::DrawList list;
    const hal::Rect viewport{0, 0, static_cast<std::int32_t>(benchExtent.width),
                             static_cast<std::int32_t>(benchExtent.height)};
    const hal::DamageRegion full(viewport);
    const auto frameView = [&](const std::array<render::TextureView, 1> &textures) {
        return render::FrameView{
            list.primitives(), list.payload(), list.transforms(),  list.clips(), {},
            textures,          list.paths(),   list.pathSegments()};
    };
    scope.build(4);
    buildInterface(list, scope, levels);
    std::array<render::TextureView, 1> textures{scope.strip()};
    if (!context->surface->render({frameView(textures), &full, {0, 0, 0, 1}})) {
        return 1;
    }
    hal::DamageRegion damage;
    telemetry::FrameStatistics gpu(512);
    double worstDirty = 0, idleGpu = 0;
    std::size_t idleSubmissions = 0;
    for (int frame = 0; frame < 480; ++frame) {
        const bool idle = frame % 8 == 7;
        if (!idle) {
            for (std::size_t n = 0; n < block.size(); ++n) {
                const double t = (frame * 400.0 + static_cast<double>(n)) / 48000;
                block[n] = static_cast<float>(0.6 * std::sin(2 * std::numbers::pi * 180 * t));
            }
            scope.append(block);
            scope.build(4);
            for (std::size_t c = 0; c < levels.size(); ++c) {
                levels[c] = 0.5f + 0.45f * std::sin(static_cast<float>(frame) * 0.07f +
                                                    static_cast<float>(c));
            }
        }
        damage.clear();
        if (!idle) {
            for (int meter = 0; meter < 8; ++meter) {
                for (int channel = 0; channel < 2; ++channel) {
                    damage.add(physical(meterRect(meter, channel)));
                }
            }
            damage.add(physical({scopeX, scopeY, scopeX + scopeWidth, scopeY + scopeHeight}));
        }
        buildInterface(list, scope, levels);
        textures = {scope.strip()};
        if (!context->surface->render({frameView(textures), &damage, {0, 0, 0, 1}})) {
            return 1;
        }
        worstDirty =
            std::max(worstDirty, static_cast<double>(damage.area()) /
                                     static_cast<double>(benchExtent.width * benchExtent.height));
        if (idle) {
            idleGpu = std::max(idleGpu, context->metal().lastGpuMilliseconds());
            idleSubmissions += context->metal().lastSubmitted() ? 1 : 0;
        } else if (frame >= 40) {
            gpu.add(context->metal().lastGpuMilliseconds());
        }
    }
    if (!reference->surface->render({frameView(textures), &full, {0, 0, 0, 1}})) {
        return 1;
    }
    std::vector<float> partial(static_cast<std::size_t>(benchExtent.width) * benchExtent.height *
                               4);
    std::vector<float> redraw(partial.size());
    if (!context->surface->readback(partial) || !reference->surface->readback(redraw)) {
        return 1;
    }
    double difference = 0;
    for (std::size_t i = 0; i < partial.size(); ++i) {
        difference = std::max(difference, static_cast<double>(std::abs(partial[i] - redraw[i])));
    }
    Report report("W6");
    report.distribution("GPU ms", gpu);
    report.below("dirty fraction", worstDirty, 0.06);
    report.below("GPU Q99 ms", gpu.quantile(0.99), 0.20);
    report.below("idle GPU ms", idleGpu, 1e-12);
    report.below("idle submissions", static_cast<double>(idleSubmissions), 1);
    report.below("difference from full redraw", difference, 1e-12);
    return report.finish();
}
}
