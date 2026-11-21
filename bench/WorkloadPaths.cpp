#include "BenchSupport.h"
#include "Workloads.h"

#include "render/DrawList.h"
#include "render/vector/VectorPath.h"

#include <cmath>
#include <numbers>

namespace prism::bench {
namespace {
void buildPaths(render::DrawList &list, render::VectorPath &path, int frame) {
    list.clear();
    const auto transform = *list.addTransform(math::Affine2D::scale(displayScale, displayScale));
    list.setTransform(transform);
    const double t = static_cast<double>(frame) / 120;
    for (int p = 0; p < 64; ++p) {
        path.clear();
        const double cx = 80 + 160 * (p % 8), cy = 45 + 90 * (p / 8);
        for (int sub = 0; sub < 4; ++sub) {
            const double radius = 36 - 7 * sub;
            constexpr int lobes = 18;
            for (int k = 0; k <= lobes; ++k) {
                const double a0 = 2 * std::numbers::pi * k / lobes;
                const double a1 = 2 * std::numbers::pi * (k + 1) / lobes;
                const double wobble = 1 + 0.18 * std::sin(3 * a0 + t * 2 + p + sub);
                const math::Vector2 point{cx + radius * wobble * std::cos(a0),
                                          cy + radius * wobble * std::sin(a0)};
                if (k == 0) {
                    path.moveTo(point);
                    continue;
                }
                const double am = (a0 + a1) / 2 - std::numbers::pi / lobes;
                const double bulge = radius * (1.25 + 0.2 * std::sin(t * 3 + k));
                path.cubicTo({cx + bulge * std::cos(am - 0.05), cy + bulge * std::sin(am - 0.05)},
                             {cx + bulge * std::cos(am + 0.05), cy + bulge * std::sin(am + 0.05)},
                             point);
            }
            path.close();
        }
        list.fillPath(path,
                      render::packColor(static_cast<std::uint8_t>(60 + 3 * p),
                                        static_cast<std::uint8_t>(200 - 2 * p), 220, 230),
                      p % 2 == 0 ? render::FillRule::nonZero : render::FillRule::evenOdd);
    }
}
}

int runPaths() {
    auto context = openContext();
    if (!context) {
        return skipCode;
    }
    render::DrawList list;
    render::VectorPath path;
    buildPaths(list, path, 0);
    const hal::DamageRegion damage({0, 0, static_cast<std::int32_t>(benchExtent.width),
                                    static_cast<std::int32_t>(benchExtent.height)});
    telemetry::FrameStatistics host(512), gpu(512);
    std::size_t segments = 0, stalls = 0, allocations = 0;
    for (int frame = 0; frame < 300; ++frame) {
        const bool measured = frame >= 20;
        countAllocations(measured);
        const Stopwatch encode;
        buildPaths(list, path, frame);
        const double encodeMs = encode.milliseconds();
        allocations += measured ? allocationCount() : 0;
        countAllocations(false);
        const render::FrameView view{
            list.primitives(), list.payload(),     list.transforms(), list.clips(), {}, {},
            list.paths(),      list.pathSegments()};
        if (!context->surface->render({view, &damage, {0.04, 0.04, 0.05, 1}})) {
            return 1;
        }
        if (measured) {
            host.add(encodeMs);
            gpu.add(context->metal().lastGpuMilliseconds());
            stalls += encodeMs > 2.0 ? 1 : 0;
        }
        segments = list.pathSegments().size();
    }
    Report report("W3");
    report.distribution("host flatten ms", host);
    report.distribution("GPU raster ms", gpu);
    report.atLeast("line segments per frame", static_cast<double>(segments), 20000);
    report.below("host flattening stalls over 2 ms", static_cast<double>(stalls), 1);
    report.below("host allocations per frame", static_cast<double>(allocations) / 280, 1);
    return report.finish();
}
}
