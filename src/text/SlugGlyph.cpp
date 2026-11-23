#include "text/SlugGlyph.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace prism::text {
namespace {
using math::Vector2;

struct Quad {
    Vector2 p1;
    Vector2 p2;
    Vector2 p3;
};

void appendCubic(std::vector<Quad> &out, Vector2 p0, Vector2 c1, Vector2 c2, Vector2 p3,
                 double tolerance) {
    const Vector2 third = p3 - c2 * 3 + c1 * 3 - p0;
    const double error = math::length(third) * std::sqrt(3.0) / 36;
    const int pieces =
        std::max(1, static_cast<int>(std::ceil(std::cbrt(error / std::max(tolerance, 1e-12)))));
    const auto point = [&](double t) {
        const double s = 1 - t;
        return p0 * (s * s * s) + c1 * (3 * s * s * t) + c2 * (3 * s * t * t) + p3 * (t * t * t);
    };
    const auto tangent = [&](double t) {
        const double s = 1 - t;
        return (c1 - p0) * (3 * s * s) + (c2 - c1) * (6 * s * t) + (p3 - c2) * (3 * t * t);
    };
    for (int i = 0; i < pieces; ++i) {
        const double a = static_cast<double>(i) / pieces;
        const double b = static_cast<double>(i + 1) / pieces;
        const double h = b - a;
        const Vector2 q0 = point(a);
        const Vector2 q3 = point(b);
        const Vector2 d1 = q0 + tangent(a) * (h / 3);
        const Vector2 d2 = q3 - tangent(b) * (h / 3);
        out.push_back({q0, (d1 * 3 - q0 + d2 * 3 - q3) * 0.25, q3});
    }
}

void appendArc(std::vector<Quad> &out, Vector2 centre, double radius, double start, double sweep) {
    const int pieces =
        std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / (std::numbers::pi / 4))));
    const double step = sweep / pieces;
    for (int i = 0; i < pieces; ++i) {
        const double a0 = start + step * i;
        const double a1 = a0 + step;
        const double reach = radius / std::cos(step / 2);
        out.push_back({centre + Vector2{std::cos(a0), std::sin(a0)} * radius,
                       centre + Vector2{std::cos((a0 + a1) / 2), std::sin((a0 + a1) / 2)} * reach,
                       centre + Vector2{std::cos(a1), std::sin(a1)} * radius});
    }
}

std::vector<Quad> quadsOf(const render::VectorPath &path, double tolerance) {
    std::vector<Quad> quads;
    const auto points = path.points();
    std::size_t index = 0;
    Vector2 current{0, 0};
    Vector2 start{0, 0};
    bool open = false;
    const auto line = [&](Vector2 to) {
        if (math::length(to - current) > 0) {
            quads.push_back({current, (current + to) * 0.5, to});
        }
        current = to;
    };

    for (const auto verb : path.verbs()) {
        switch (verb) {
        case render::PathVerb::move:
            if (open) {
                line(start);
            }
            current = start = points[index++];
            open = true;
            break;
        case render::PathVerb::line:
            line(points[index++]);
            break;
        case render::PathVerb::quad:
            quads.push_back({current, points[index], points[index + 1]});
            current = points[index + 1];
            index += 2;
            break;
        case render::PathVerb::cubic:
            appendCubic(quads, current, points[index], points[index + 1], points[index + 2],
                        tolerance);
            current = points[index + 2];
            index += 3;
            break;
        case render::PathVerb::arc: {
            const Vector2 centre = points[index];
            const double radius = points[index + 1].x;
            const double from = points[index + 2].x;
            const double sweep = points[index + 2].y;
            appendArc(quads, centre, radius, from, sweep);
            current = centre + Vector2{std::cos(from + sweep), std::sin(from + sweep)} * radius;
            index += 3;
            break;
        }
        case render::PathVerb::close:
            line(start);
            open = false;
            break;
        }
    }
    if (open) {
        line(start);
    }
    return quads;
}
}

SlugGlyph buildSlugGlyph(const render::VectorPath &outline, double tolerance) {
    const std::vector<Quad> quads = quadsOf(outline, tolerance);
    SlugGlyph glyph;
    if (quads.empty()) {
        return glyph;
    }

    double x0 = std::numeric_limits<double>::infinity();
    double y0 = x0;
    double x1 = -x0;
    double y1 = -x0;
    for (const Quad &q : quads) {
        for (const Vector2 p : {q.p1, q.p2, q.p3}) {
            x0 = std::min(x0, p.x);
            y0 = std::min(y0, p.y);
            x1 = std::max(x1, p.x);
            y1 = std::max(y1, p.y);
        }
    }
    const double margin = std::max(x1 - x0, y1 - y0) * 0.02 + 1e-6;
    x0 -= margin;
    y0 -= margin;
    x1 += margin;
    y1 += margin;

    const auto bandCount =
        static_cast<std::uint32_t>(std::clamp<std::size_t>(quads.size() / 2, 1, slugMaxBands));
    std::array<std::vector<std::uint32_t>, slugMaxBands> horizontal;
    std::array<std::vector<std::uint32_t>, slugMaxBands> vertical;
    const double bandHeight = (y1 - y0) / bandCount;
    const double bandWidth = (x1 - x0) / bandCount;
    for (std::uint32_t i = 0; i < quads.size(); ++i) {
        const Quad &q = quads[i];
        const double qy0 = std::min({q.p1.y, q.p2.y, q.p3.y});
        const double qy1 = std::max({q.p1.y, q.p2.y, q.p3.y});
        const double qx0 = std::min({q.p1.x, q.p2.x, q.p3.x});
        const double qx1 = std::max({q.p1.x, q.p2.x, q.p3.x});
        for (std::uint32_t band = 0; band < bandCount; ++band) {
            const double low = y0 + bandHeight * band;
            if (qy1 >= low - margin && qy0 <= low + bandHeight + margin) {
                horizontal[band].push_back(i);
            }
            const double left = x0 + bandWidth * band;
            if (qx1 >= left - margin && qx0 <= left + bandWidth + margin) {
                vertical[band].push_back(i);
            }
        }
    }

    std::size_t indices = 0;
    for (std::uint32_t band = 0; band < bandCount; ++band) {
        indices += horizontal[band].size() + vertical[band].size();
    }
    const std::size_t tables = slugHeader + 4 * static_cast<std::size_t>(bandCount);
    const std::size_t curveBase = tables + indices;
    glyph.blob.assign(curveBase + quads.size() * slugCurveFloats, 0.0f);
    glyph.x0 = static_cast<float>(x0);
    glyph.y0 = static_cast<float>(y0);
    glyph.x1 = static_cast<float>(x1);
    glyph.y1 = static_cast<float>(y1);
    glyph.blob[0] = glyph.x0;
    glyph.blob[1] = glyph.y0;
    glyph.blob[2] = glyph.x1;
    glyph.blob[3] = glyph.y1;
    glyph.blob[4] = static_cast<float>(quads.size());
    glyph.blob[5] = static_cast<float>(bandCount);
    glyph.blob[6] = static_cast<float>(bandCount);
    glyph.blob[7] = static_cast<float>(curveBase);

    std::size_t cursor = tables;
    const auto table = [&](std::size_t entry, const std::vector<std::uint32_t> &list) {
        glyph.blob[entry] = static_cast<float>(cursor);
        glyph.blob[entry + 1] = static_cast<float>(list.size());
        for (const std::uint32_t index : list) {
            glyph.blob[cursor++] = static_cast<float>(index);
        }
    };
    for (std::uint32_t band = 0; band < bandCount; ++band) {
        table(slugHeader + 2 * band, horizontal[band]);
    }
    for (std::uint32_t band = 0; band < bandCount; ++band) {
        table(slugHeader + 2 * (bandCount + band), vertical[band]);
    }

    for (std::size_t i = 0; i < quads.size(); ++i) {
        float *curve = glyph.blob.data() + curveBase + i * slugCurveFloats;
        const Quad &q = quads[i];
        curve[0] = static_cast<float>(q.p1.x);
        curve[1] = static_cast<float>(q.p1.y);
        curve[2] = static_cast<float>(q.p2.x);
        curve[3] = static_cast<float>(q.p2.y);
        curve[4] = static_cast<float>(q.p3.x);
        curve[5] = static_cast<float>(q.p3.y);
    }
    return glyph;
}
}
