#include "BenchSupport.h"
#include "Workloads.h"

#include "render/DrawList.h"
#include "render/GradientTable.h"

#include <array>
#include <cmath>
#include <numbers>

namespace prism::bench {
namespace {
void buildDense(render::DrawList &list, std::uint32_t gradientRow, int frame) {
    list.clear();
    const auto transform = *list.addTransform(math::Affine2D::scale(displayScale, displayScale));
    list.setTransform(transform);
    constexpr int columns = 125, rows = 80;
    constexpr float cellX = 1280.0f / columns, cellY = 720.0f / rows;
    const float wobble = 0.5f * std::sin(static_cast<float>(frame) * 0.05f);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const int i = row * columns + column;
            const float x = static_cast<float>(column) * cellX + wobble;
            const float y = static_cast<float>(row) * cellY;
            const render::RectF cell{x + 1, y + 1, x + cellX - 1, y + cellY - 1};
            const auto color = render::packColor(static_cast<std::uint8_t>(40 + i % 180),
                                                 static_cast<std::uint8_t>(80 + i % 120),
                                                 static_cast<std::uint8_t>(200 - i % 150), 255);
            switch (i % 5) {
            case 0:
                list.roundedRect(
                    cell, {2, 2, 2, 2}, color,
                    render::BorderStyle{{1, 1, 1, 1}, render::packColor(10, 10, 10, 255)});
                break;
            case 1:
                list.arc({(cell.x0 + cell.x1) / 2, (cell.y0 + cell.y1) / 2}, 2.6f, 0.8f,
                         static_cast<float>(i) * 0.1f, 2.2f, math::ArcCap::round, color);
                break;
            case 2:
                list.boxShadow(cell, 2, 1.5f, render::packColor(0, 0, 0, 160));
                break;
            case 3: {
                const float radius = std::min(cellX, cellY) / 2 - 1;
                list.roundedRect(cell, {radius, radius, radius, radius}, color);
                break;
            }
            default:
                list.setLinearGradient(gradientRow, {cell.x0, cell.y0}, {cell.x1, cell.y1});
                list.roundedRect(cell, {1, 1, 1, 1}, color);
                list.clearGradient();
                break;
            }
        }
    }
}
}

int runDense() {
    auto context = openContext();
    if (!context) {
        return skipCode;
    }
    render::GradientTable gradients;
    const std::array<render::GradientStop, 2> stops{
        render::GradientStop{0, render::packColor(255, 120, 40, 255)},
        render::GradientStop{1, render::packColor(40, 60, 255, 255)}};
    const auto row = gradients.add(stops, render::GradientSpace::oklab);
    render::DrawList list;
    buildDense(list, row, 0);
    const hal::DamageRegion damage({0, 0, static_cast<std::int32_t>(benchExtent.width),
                                    static_cast<std::int32_t>(benchExtent.height)});
    telemetry::FrameStatistics host(512), gpu(512);
    std::size_t drawCalls = 0, uploadBytes = 0, primitives = 0;
    for (int frame = 0; frame < 300; ++frame) {
        const Stopwatch encode;
        buildDense(list, row, frame);
        const double encodeMs = encode.milliseconds();
        const render::FrameView view{list.primitives(), list.payload(),     list.transforms(),
                                     list.clips(),      gradients.rows(),   {},
                                     list.paths(),      list.pathSegments()};
        if (!context->surface->render({view, &damage, {0.05, 0.05, 0.06, 1}})) {
            return 1;
        }
        if (frame >= 20) {
            host.add(encodeMs);
            gpu.add(context->metal().lastGpuMilliseconds());
        }
        drawCalls = context->metal().lastDrawCalls();
        uploadBytes = context->metal().lastUploadBytes();
        primitives = list.primitives().size();
    }
    Report report("W1");
    report.distribution("host encode ms", host);
    report.distribution("GPU ms", gpu);
    report.below("primitives emitted", static_cast<double>(primitives), 10001);
    report.atLeast("primitives emitted", static_cast<double>(primitives), 10000);
    report.below("draw calls", static_cast<double>(drawCalls), 9);
    report.below("upload bytes per primitive", static_cast<double>(uploadBytes) / 10000,
                 472.0 / 6);
    report.below("host encode Q50 ms", host.quantile(0.5), 0.25);
    return report.finish();
}
}
