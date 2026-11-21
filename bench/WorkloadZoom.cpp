#include "BenchSupport.h"
#include "BenchText.h"
#include "Workloads.h"

#include "render/DrawList.h"

#include <cmath>
#include <string>
#include <vector>

namespace prism::bench {
namespace {
void buildLabels(render::DrawList &list, BenchText &text, const std::vector<std::string> &labels,
                 double zoom) {
    list.clear();
    text.engine->beginFrame();
    const double scale = displayScale * zoom;
    const auto transform = *list.addTransform(math::Affine2D::translation(1280, 720) *
                                              math::Affine2D::scale(scale, scale) *
                                              math::Affine2D::translation(-640, -360));
    list.setTransform(transform);
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const math::Vector2 at{20.0 + 50 * static_cast<double>(i % 25),
                               20.0 + 34 * static_cast<double>(i / 25)};
        text.engine->draw(list, text.shaper->shape({labels[i], 12}), at,
                          {render::packColor(230, 230, 230, 255), false, true}, 12, scale);
    }
}
}

int runZoom() {
    auto context = openContext();
    auto text = loadBenchText(0);
    if (!context || !text) {
        return skipCode;
    }
    std::vector<std::string> labels;
    for (int i = 0; i < 500; ++i) {
        labels.push_back("Gain " + std::to_string(i % 64) + " dB");
    }
    render::DrawList list;
    buildLabels(list, *text, labels, 1.0);
    const hal::DamageRegion damage({0, 0, static_cast<std::int32_t>(benchExtent.width),
                                    static_cast<std::int32_t>(benchExtent.height)});
    const auto warm = [&] {
        const auto views = text->engine->textures();
        const render::FrameView view{list.primitives(),
                                     list.payload(),
                                     list.transforms(),
                                     list.clips(),
                                     {},
                                     std::span(views.data(), views.size()),
                                     list.paths(),
                                     list.pathSegments()};
        return context->surface->render({view, &damage, {0, 0, 0, 1}});
    };
    if (!warm()) {
        return 1;
    }
    const auto rasterized = text->engine->rasterizations();
    const auto evictions = text->engine->atlas(text::TextTier::mtsdf).evictions();
    const auto pages = text->engine->atlas(text::TextTier::mtsdf).pageCount();
    telemetry::FrameStatistics frameTime(512), gpu(512);
    for (int frame = 0; frame < 300; ++frame) {
        const double zoom = 0.25 * std::pow(3.0 / 0.25, frame / 299.0);
        const Stopwatch total;
        buildLabels(list, *text, labels, zoom);
        if (!warm()) {
            return 1;
        }
        frameTime.add(total.milliseconds());
        gpu.add(context->metal().lastGpuMilliseconds());
    }
    Report report("W4");
    report.distribution("frame ms", frameTime);
    report.distribution("GPU ms", gpu);
    report.below("glyph re-rasterizations",
                 static_cast<double>(text->engine->rasterizations() - rasterized), 1);
    report.below(
        "atlas rebuilds",
        static_cast<double>(text->engine->atlas(text::TextTier::mtsdf).evictions() - evictions +
                            text->engine->atlas(text::TextTier::mtsdf).pageCount() - pages),
        1);
    report.below("frame Q99 ms", frameTime.quantile(0.99), 1.5);
    return report.finish();
}
}
