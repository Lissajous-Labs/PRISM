#include "../support/AllocGuard.h"
#include "../support/Pcg.h"
#include "math/SdfShapes.h"
#include "math/SpringCable.h"
#include "math/WangFlatten.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>

namespace {
using namespace prism::math;

template <class Curve> double flattenDeviation(Curve curve, std::uint32_t segments) {
    double worst = 0;
    for (std::uint32_t i = 0; i < segments; ++i) {
        const double t0 = static_cast<double>(i) / segments;
        const double t1 = static_cast<double>(i + 1) / segments;
        const Vector2 a = curve(t0);
        const Vector2 b = curve(t1);
        for (int sample = 1; sample < 64; ++sample) {
            worst = std::max(worst, segment(curve(t0 + (t1 - t0) * sample / 64), a, b));
        }
    }
    return worst;
}

bool checkFlattening(prism::test::Pcg &random) {
    double worst = 0;
    for (int i = 0; i < 2000; ++i) {
        const double scale = random.range(1, 1000);
        const auto point = [&] {
            return Vector2{random.range(-scale, scale), random.range(-scale, scale)};
        };
        const Vector2 p0 = point();
        const Vector2 p1 = point();
        const Vector2 p2 = point();
        const Vector2 p3 = point();
        worst = std::max(worst,
                         flattenDeviation([&](double t) { return quadraticPoint(p0, p1, p2, t); },
                                          quadraticSegments(p0, p1, p2)));
        worst = std::max(worst,
                         flattenDeviation([&](double t) { return cubicPoint(p0, p1, p2, p3, t); },
                                          cubicSegments(p0, p1, p2, p3)));
        const double radius = random.range(0.2, 500);
        const double sweep = random.range(0, 2 * std::numbers::pi);
        const auto segments = arcSegments(sweep, radius);
        worst = std::max(worst, radius * (1 - std::cos(sweep / segments / 2)));
    }
    const double h = 1e-6;
    const double slope = (parabolaArcLength(0.7 + h) - parabolaArcLength(0.7 - h)) / (2 * h);
    std::cout << "Flatten deviation: " << worst << '\n';
    return worst <= defaultFlattenTolerance + 1e-9 &&
           std::abs(slope - std::sqrt(1 + 0.49)) < 1e-8 &&
           quadraticSegments({0, 0}, {1, 1}, {2, 2}) == 1;
}

bool checkSpring() {
    SpringState stepped{10, -3};
    for (int i = 0; i < 240; ++i) {
        stepped = criticalSpringStep(stepped, 2, 12, 1.0 / 240);
    }
    const auto direct = criticalSpringStep({10, -3}, 2, 12, 1);
    SpringState rest{0, 0};
    bool monotonic = true;
    double previous = 0;
    for (int i = 0; i < 600; ++i) {
        rest = criticalSpringStep(rest, 1, 20, 1.0 / 120);
        monotonic = monotonic && rest.position >= previous && rest.position <= 1;
        previous = rest.position;
    }
    return std::abs(stepped.position - direct.position) < 1e-12 &&
           std::abs(stepped.velocity - direct.velocity) < 1e-10 && monotonic &&
           std::abs(rest.position - 1) < 1e-9;
}

bool checkCable() {
    VerletCable<32> cable({0, 0}, {300, 0}, 400);
    prism::test::AllocGuard guard;
    for (int frame = 0; frame < 2000; ++frame) {
        cable.setEnds({0, 0}, {300, 0});
        cable.step(1.0 / 120, {0, 980}, 0.98, 128);
    }
    const auto &points = cable.points();
    double worstStretch = 0;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        worstStretch =
            std::max(worstStretch, length(points[i + 1] - points[i]) / cable.segmentLength() - 1);
    }
    std::cout << "Cable stretch: " << worstStretch << '\n';
    return guard.counts().empty() && points.front() == Vector2{0, 0} &&
           points.back() == Vector2{300, 0} && points[16].y > 50 && worstStretch < 1e-3;
}
}

int main() {
    prism::test::Pcg random(41);
    if (!checkFlattening(random) || !checkSpring() || !checkCable()) {
        std::cerr << "Flattening and spring contract failed\n";
        return 1;
    }
    return 0;
}
