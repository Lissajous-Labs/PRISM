#pragma once

#include "math/Vector2.h"
#include "render/vector/VectorPath.h"

#include <cmath>
#include <functional>
#include <numbers>
#include <string>
#include <vector>

namespace prism::test {
struct ContourShape {
    std::string name;
    render::VectorPath path;
    std::vector<std::function<math::Vector2(double)>> edges;
    std::vector<std::function<math::Vector2(double)>> tangents;
};

inline math::Vector2 quadraticPoint(math::Vector2 a, math::Vector2 b, math::Vector2 c, double t) {
    const double s = 1 - t;
    return a * (s * s) + b * (2 * s * t) + c * (t * t);
}

inline math::Vector2 cubicPoint(math::Vector2 a, math::Vector2 b, math::Vector2 c, math::Vector2 d,
                                double t) {
    const double s = 1 - t;
    return a * (s * s * s) + b * (3 * s * s * t) + c * (3 * s * t * t) + d * (t * t * t);
}

inline math::Vector2 cubicTangent(math::Vector2 a, math::Vector2 b, math::Vector2 c,
                                  math::Vector2 d, double t) {
    const double s = 1 - t;
    return (b - a) * (3 * s * s) + (c - b) * (6 * s * t) + (d - c) * (3 * t * t);
}

inline void addCircle(ContourShape &shape, math::Vector2 centre, double radius, bool reverse) {
    constexpr int pieces = 8;
    const double step = (reverse ? -2 : 2) * std::numbers::pi / pieces;
    const auto at = [&](double angle, double r) {
        return centre + math::Vector2{std::cos(angle), std::sin(angle)} * r;
    };
    shape.path.moveTo(at(0, radius));
    for (int i = 0; i < pieces; ++i) {
        const double a0 = step * i;
        const double a1 = a0 + step;
        const math::Vector2 p0 = at(a0, radius);
        const math::Vector2 p1 = at((a0 + a1) / 2, radius / std::cos(step / 2));
        const math::Vector2 p2 = at(a1, radius);
        shape.path.quadTo(p1, p2);
        shape.edges.emplace_back([=](double t) { return quadraticPoint(p0, p1, p2, t); });
        shape.tangents.emplace_back(
            [=](double t) { return (p1 - p0) * (2 * (1 - t)) + (p2 - p1) * (2 * t); });
    }
    shape.path.close();
}

inline void addSegment(ContourShape &shape, math::Vector2 a, math::Vector2 b) {
    shape.edges.emplace_back([=](double t) { return a + (b - a) * t; });
    shape.tangents.emplace_back([=](double) { return b - a; });
}

inline void addPolygon(ContourShape &shape, const std::vector<math::Vector2> &points) {
    shape.path.moveTo(points[0]);
    for (std::size_t i = 0; i < points.size(); ++i) {
        const math::Vector2 a = points[i];
        const math::Vector2 b = points[(i + 1) % points.size()];
        if (i + 1 < points.size()) {
            shape.path.lineTo(b);
        }
        addSegment(shape, a, b);
    }
    shape.path.close();
}

inline std::vector<ContourShape> contourShapes() {
    std::vector<ContourShape> list;

    ContourShape ring{"ring", {}, {}, {}};
    addCircle(ring, {24, 24}, 18, false);
    addCircle(ring, {24, 24}, 9, true);
    list.push_back(std::move(ring));

    ContourShape spike{"spike", {}, {}, {}};
    addPolygon(spike, {{6, 42}, {24, 4}, {42, 42}, {24, 30}});
    list.push_back(std::move(spike));

    ContourShape wave{"wave", {}, {}, {}};
    const math::Vector2 a{6, 30};
    const math::Vector2 b{14, 6};
    const math::Vector2 c{34, 54};
    const math::Vector2 d{42, 18};
    wave.path.moveTo(a);
    wave.path.cubicTo(b, c, d);
    wave.path.lineTo({42, 42});
    wave.path.lineTo({6, 42});
    wave.path.close();
    wave.edges.emplace_back([=](double t) { return cubicPoint(a, b, c, d, t); });
    wave.tangents.emplace_back([=](double t) { return cubicTangent(a, b, c, d, t); });
    addSegment(wave, d, {42, 42});
    addSegment(wave, {42, 42}, {6, 42});
    addSegment(wave, {6, 42}, a);
    list.push_back(std::move(wave));

    ContourShape tear{"teardrop", {}, {}, {}};
    const math::Vector2 apex{24, 4};
    const math::Vector2 c1{52, 40};
    const math::Vector2 c2{-4, 40};
    tear.path.moveTo(apex);
    tear.path.cubicTo(c1, c2, apex);
    tear.path.close();
    tear.edges.emplace_back([=](double t) { return cubicPoint(apex, c1, c2, apex, t); });
    tear.tangents.emplace_back([=](double t) { return cubicTangent(apex, c1, c2, apex, t); });
    list.push_back(std::move(tear));
    return list;
}
}
