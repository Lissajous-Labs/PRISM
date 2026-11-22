#include "../support/Pcg.h"
#include "math/SdfShapes.h"
#include "render/vector/FineRaster.h"
#include "render/vector/StrokeExpander.h"
#include "render/vector/VectorPath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

namespace {
using namespace prism;
using math::Vector2;

double polylineDistance(Vector2 p, const render::Polyline &polyline) {
    double best = INFINITY;
    for (std::size_t i = 0; i + 1 < polyline.points.size(); ++i) {
        best = std::min(best, math::segment(p, polyline.points[i], polyline.points[i + 1]));
    }
    return best;
}

bool checkHausdorff(test::Pcg &random) {
    double worst = 0;
    for (int trial = 0; trial < 1500; ++trial) {
        const double scale = random.range(1, 800);
        const auto point = [&] {
            return Vector2{random.range(-scale, scale), random.range(-scale, scale)};
        };
        const Vector2 p0 = point();
        const Vector2 p1 = point();
        const Vector2 p2 = point();
        const Vector2 p3 = point();
        const double turn = random.range(0, 6.28);
        const math::Affine2D transform =
            math::Affine2D::rotation(turn) *
            math::Affine2D::scale(random.range(0.5, 2), random.range(0.5, 2));
        render::VectorPath path;
        const int kind = trial % 3;
        path.moveTo(p0);
        if (kind == 0) {
            path.quadTo(p1, p2);
        } else if (kind == 1) {
            path.cubicTo(p1, p2, p3);
        }
        const double radius = random.range(0.5, scale);
        const double start = random.range(0, 6.28);
        const double sweep = random.range(-6.28, 6.28);
        if (kind == 2) {
            path.clear();
            path.arcTo(p0, radius, start, sweep);
        }
        std::vector<render::Polyline> polylines;
        render::flattenPolylines(path, transform, 0.125, polylines);
        if (polylines.size() != 1) {
            return false;
        }
        for (int i = 0; i <= 256; ++i) {
            const double t = i / 256.0;
            const double s = 1 - t;
            Vector2 local;
            if (kind == 0) {
                local = p0 * (s * s) + p1 * (2 * s * t) + p2 * (t * t);
            } else if (kind == 1) {
                local = p0 * (s * s * s) + p1 * (3 * s * s * t) + p2 * (3 * s * t * t) +
                        p3 * (t * t * t);
            } else {
                const double angle = start + sweep * t;
                local = p0 + Vector2{std::cos(angle), std::sin(angle)} * radius;
            }
            const auto mapped = transform.apply(math::Point2{local.x, local.y});
            worst = std::max(worst, polylineDistance({mapped.x, mapped.y}, polylines[0]));
        }
    }
    std::cout << "Flattening Hausdorff deviation: " << worst << '\n';
    return worst <= 0.125 + 1e-9;
}

double strokeArea(const std::vector<render::Polyline> &centerlines,
                  const render::StrokeStyle &style) {
    render::StrokeScratch scratch;
    render::PolylineBuffer outlines;
    render::strokeOutlines(centerlines, style, scratch, outlines);
    std::vector<render::LineSegment> segments;
    render::appendFill(outlines.view(), segments);
    render::PathTiler tiler;
    tiler.run(segments, 256, 256);
    std::vector<float> coverage(256 * 256);
    render::rasterizeCoverage(tiler, render::FillRule::nonZero, 256, 256, coverage);
    double area = 0;
    for (const float value : coverage) {
        area += value;
    }
    return area;
}

bool near(double actual, double expected, double tolerance) {
    const bool ok = std::abs(actual - expected) <= tolerance;
    if (!ok) {
        std::cerr << "Stroke area " << actual << " expected " << expected << '\n';
    }
    return ok;
}

bool checkStrokes() {
    const std::vector<render::Polyline> line{{{{50, 100}, {150, 100}}, false}};
    render::StrokeStyle style;
    style.width = 10;
    bool ok = near(strokeArea(line, style), 1000, 0.5);
    style.cap = render::LineCap::square;
    ok = ok && near(strokeArea(line, style), 1100, 0.5);
    style.cap = render::LineCap::round;
    ok = ok && near(strokeArea(line, style), 1000 + std::numbers::pi * 25,
                    2 * 0.125 * std::numbers::pi * 5);
    const std::vector<render::Polyline> corner{{{{50, 50}, {150, 50}, {150, 150}}, false}};
    style.cap = render::LineCap::butt;
    style.join = render::LineJoin::miter;
    ok = ok && near(strokeArea(corner, style), 2000 - 25 + 25, 0.5);
    style.join = render::LineJoin::bevel;
    ok = ok && near(strokeArea(corner, style), 2000 - 25 + 12.5, 0.5);
    style.join = render::LineJoin::round;
    ok = ok && near(strokeArea(corner, style), 2000 - 25 + std::numbers::pi * 25 / 4, 1.0);
    const std::vector<render::Polyline> sharp{{{{40, 60}, {200, 80}, {40, 100}}, false}};
    style.join = render::LineJoin::miter;
    const double limited = strokeArea(sharp, style);
    style.join = render::LineJoin::bevel;
    ok = ok && near(limited, strokeArea(sharp, style), 1e-6);
    const std::array<double, 2> dashes{10, 10};
    style.dashes = dashes;
    style.join = render::LineJoin::miter;
    ok = ok && near(strokeArea(line, style), 500, 0.5);
    style.dashes = {};
    const std::vector<render::Polyline> square{
        {{{50, 50}, {150, 50}, {150, 150}, {50, 150}}, true}};
    ok = ok && near(strokeArea(square, style), 110 * 110 - 90 * 90, 0.5);
    return ok;
}
}

int main() {
    test::Pcg random(83);
    if (!checkHausdorff(random) || !checkStrokes()) {
        std::cerr << "Flattening and stroke contract failed\n";
        return 1;
    }
    return 0;
}
