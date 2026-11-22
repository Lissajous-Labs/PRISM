#include "../support/AllocGuard.h"
#include "../support/CoverageOracle.h"
#include "../support/Pcg.h"
#include "../support/PolygonClipOracle.h"
#include "render/vector/FineRaster.h"
#include "render/vector/VectorPath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <vector>

namespace {
using namespace prism;
using Contours = std::vector<std::vector<math::Vector2>>;
constexpr std::uint32_t canvas = 64;

Contours randomShape(test::Pcg &random, int kind) {
    Contours contours;
    if (kind == 0) {
        const int vertices = 3 + static_cast<int>(random.next() % 10);
        auto &contour = contours.emplace_back();
        for (int i = 0; i < vertices; ++i) {
            contour.push_back({random.range(-8, 72), random.range(-8, 72)});
        }
    } else if (kind == 1) {
        for (int r = 0; r < 3; ++r) {
            const double x = std::floor(random.range(-4, 56));
            const double y = std::floor(random.range(-4, 56));
            const double w = std::floor(random.range(1, 30));
            const double h = std::floor(random.range(1, 30));
            contours.push_back({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}});
        }
    } else if (kind == 2) {
        const math::Vector2 a{random.range(0, 64), random.range(0, 64)};
        const math::Vector2 b{random.range(0, 64), random.range(0, 64)};
        const math::Vector2 offset{random.range(-0.3, 0.3), random.range(-0.3, 0.3)};
        contours.push_back({a, b, b + offset});
    } else {
        render::VectorPath path;
        const auto point = [&] {
            return math::Vector2{random.range(-4, 68), random.range(-4, 68)};
        };
        path.moveTo(point());
        path.cubicTo(point(), point(), point());
        path.quadTo(point(), point());
        path.cubicTo(point(), point(), point());
        path.close();
        if (random.unit() < 0.5) {
            path.arcTo({32, 32}, random.range(4, 30), random.range(0, 6), random.range(-6, 6));
            path.close();
        }
        std::vector<render::Polyline> polylines;
        render::flattenPolylines(path, {}, 0.125, polylines);
        for (const auto &polyline : polylines) {
            contours.push_back(polyline.points);
        }
    }
    return contours;
}

std::vector<render::LineSegment> segmentsOf(const Contours &contours) {
    std::vector<render::Polyline> polylines;
    for (const auto &contour : contours) {
        polylines.push_back({contour, true});
    }
    std::vector<render::LineSegment> segments;
    render::appendFill(polylines, segments);
    return segments;
}

bool checkAgainstClipping(test::Pcg &random) {
    render::PathTiler tiler;
    std::vector<float> coverage(canvas * canvas);
    double worst = 0;
    for (int trial = 0; trial < 500; ++trial) {
        const Contours contours = randomShape(random, trial % 4);
        const auto rule = trial % 2 == 0 ? render::FillRule::nonZero : render::FillRule::evenOdd;
        tiler.run(segmentsOf(contours), canvas, canvas);
        render::rasterizeCoverage(tiler, rule, canvas, canvas, coverage);
        for (std::uint32_t y = 0; y < canvas; ++y) {
            for (std::uint32_t x = 0; x < canvas; ++x) {
                const double winding =
                    test::pixelWinding(contours, static_cast<int>(x), static_cast<int>(y));
                const double magnitude = std::abs(winding);
                const double expected = rule == render::FillRule::nonZero
                                            ? std::min(magnitude, 1.0)
                                            : 1 - std::abs(std::fmod(magnitude, 2.0) - 1);
                worst = std::max(worst, std::abs(coverage[y * canvas + x] - expected));
            }
        }
    }
    std::cout << "Vector coverage error against clipping oracle: " << worst << '\n';
    return worst < 1e-3;
}

bool checkAgainstSampling(test::Pcg &random) {
    render::PathTiler tiler;
    std::vector<float> coverage(canvas * canvas);
    double worst = 0;
    for (int trial = 0; trial < 20; ++trial) {
        const math::Vector2 center{random.range(20, 44), random.range(20, 44)};
        Contours contours(1);
        for (int i = 0; i < 9; ++i) {
            const double angle = 2 * std::numbers::pi * i / 9;
            const double radius = random.range(8, 20);
            contours[0].push_back(center +
                                  math::Vector2{std::cos(angle), std::sin(angle)} * radius);
        }
        tiler.run(segmentsOf(contours), canvas, canvas);
        render::rasterizeCoverage(tiler, render::FillRule::nonZero, canvas, canvas, coverage);
        std::vector<test::Point> points;
        for (const auto &p : contours[0]) {
            points.push_back({p.x, p.y});
        }
        const test::Contour contour(points);
        for (std::uint32_t y = 8; y < 56; y += 3) {
            for (std::uint32_t x = 8; x < 56; x += 3) {
                const double sampled =
                    test::stratifiedCoverage(std::span<const test::Contour>(&contour, 1),
                                             {static_cast<double>(x), static_cast<double>(y)});
                worst = std::max(worst, std::abs(coverage[y * canvas + x] - sampled));
            }
        }
    }
    std::cout << "Vector coverage error against sampling oracle: " << worst << '\n';
    return worst < 3e-3;
}

bool checkSteadyState() {
    render::PathTiler tiler;
    std::vector<float> coverage(canvas * canvas);
    const auto segments = segmentsOf({{{4, 4}, {60, 10}, {30, 60}}});
    tiler.run(segments, canvas, canvas);
    prism::test::AllocGuard guard;
    tiler.run(segments, canvas, canvas);
    render::rasterizeCoverage(tiler, render::FillRule::nonZero, canvas, canvas, coverage);
    return guard.counts().empty();
}

bool checkWindingRange() {
    constexpr std::int64_t scale = 1048576;
    constexpr std::array<std::int64_t, 5> turns{0, 4096, std::int64_t{1} << 24,
                                                std::int64_t{1} << 32, std::int64_t{1} << 40};
    for (const auto turn : turns) {
        for (int quarter = 0; quarter <= 8; ++quarter) {
            const std::int64_t fixed = turn * scale + quarter * (scale / 4);
            const float nonZero = turn == 0 ? std::min(quarter * 0.25f, 1.0f) : 1.0f;
            const float evenOdd = quarter <= 4 ? quarter * 0.25f : 2 - quarter * 0.25f;
            for (const int direction : {-1, 1}) {
                if (render::windingToAlpha(direction * fixed, render::FillRule::nonZero) !=
                        nonZero or
                    render::windingToAlpha(direction * fixed, render::FillRule::evenOdd) !=
                        evenOdd) {
                    return false;
                }
            }
        }
    }
    for (const auto limit :
         {std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max()}) {
        if (render::windingToAlpha(limit, render::FillRule::nonZero) != 1 or
            render::windingToAlpha(limit, render::FillRule::evenOdd) != 0) {
            return false;
        }
    }
    return true;
}
}

int main() {
    test::Pcg random(71);
    if (not checkAgainstClipping(random) or not checkAgainstSampling(random) or
        not checkSteadyState() or not checkWindingRange()) {
        std::cerr << "Vector rasterizer contract failed\n";
        return 1;
    }
    return 0;
}
