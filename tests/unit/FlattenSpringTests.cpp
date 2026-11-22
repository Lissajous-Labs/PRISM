#include "../support/AllocGuard.h"
#include "../support/Pcg.h"
#include "math/SdfShapes.h"
#include "math/SpringCable.h"
#include "math/WangFlatten.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
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
        stepped = springStep(stepped, 2, {12}, 1.0 / 240);
    }
    const auto direct = springStep({10, -3}, 2, {12}, 1);
    SpringState rest{0, 0};
    bool monotonic = true;
    double previous = 0;
    for (int i = 0; i < 600; ++i) {
        rest = springStep(rest, 1, {20}, 1.0 / 120);
        monotonic = monotonic && rest.position >= previous && rest.position <= 1;
        previous = rest.position;
    }
    return std::abs(stepped.position - direct.position) < 1e-12 &&
           std::abs(stepped.velocity - direct.velocity) < 1e-10 && monotonic &&
           std::abs(rest.position - 1) < 1e-9;
}

bool checkFrameRateInvariance() {
    double worst = 0;
    for (const double zeta : {0.0, 0.65, 0.999999, 1.0, 1.000001, 1.45, 4.0}) {
        const SpringParameters spring{42, zeta};
        const auto exact = springStep({0, 0}, 1, spring, 0.5);
        for (const double rate : {60.0, 120.0, 240.0}) {
            SpringState state{0, 0};
            for (int i = 0; i < static_cast<int>(rate / 2); ++i) {
                state = springStep(state, 1, spring, 1 / rate);
            }
            worst = std::max(worst, std::abs(state.position - exact.position));
        }
        SpringState variable{0, 0};
        double elapsed = 0;
        for (int i = 0; elapsed < 0.5; ++i) {
            const double dt = std::min(0.5 - elapsed, (i % 3 == 0 ? 1.0 / 60 : 1.0 / 144));
            variable = springStep(variable, 1, spring, dt);
            elapsed += dt;
        }
        worst = std::max(worst, std::abs(variable.position - exact.position));
    }
    std::cout << "Spring semigroup deviation across 60/120/240 Hz and VRR: " << worst << '\n';
    return worst < 1e-7;
}

bool checkSettling() {
    const double canonical = criticalSettlingTime(1, 0.01);
    const double drawer = criticalSettlingTime(42, 0.01);
    const double residual = std::exp(-canonical) * (1 + canonical) - 0.01;
    const auto ninety = [] {
        double low = 0, high = 1;
        for (int i = 0; i < 200; ++i) {
            const double mid = (low + high) / 2;
            (springStep({0, 0}, 1, {42}, mid).position < 0.9 ? low : high) = mid;
        }
        return low;
    }();
    const auto snapped =
        springStep({0.3, 4}, 1, {std::numeric_limits<double>::infinity()}, 1.0 / 120);
    std::cout << "Critical 1% settling: " << canonical << " / omega, drawer " << drawer * 1000
              << " ms, 90% at " << ninety * 1000 << " ms\n";
    return std::abs(canonical - 6.63835) < 5e-6 && std::abs(residual) < 1e-14 &&
           std::abs(drawer - 0.158056) < 1e-5 && std::abs(ninety - 0.0924) < 5e-4 &&
           snapped.position == 1 && snapped.velocity == 0;
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
    if (!checkFlattening(random) || !checkSpring() || !checkFrameRateInvariance() ||
        !checkSettling() || !checkCable()) {
        std::cerr << "Flattening and spring contract failed\n";
        return 1;
    }
    return 0;
}
