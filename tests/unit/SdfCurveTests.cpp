#include "../support/AllocGuard.h"
#include "../support/DistanceOracle.h"
#include "../support/Pcg.h"
#include "math/Sdf.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>

namespace {
using namespace prism::math;

bool checkBeziers(prism::test::Pcg &random) {
    double worst = 0;
    for (int i = 0; i < 4000; ++i) {
        const double scale = random.range(1, 300);
        const auto point = [&] {
            return Vector2{random.range(-scale, scale), random.range(-scale, scale)};
        };
        Vector2 p0 = point();
        Vector2 p1 = point();
        Vector2 p2 = point();
        if (i % 4 == 1) {
            p1 = (p0 + p2) * 0.5 + Vector2{1e-7, -1e-7};
        } else if (i % 4 == 2) {
            p1 = p0;
        } else if (i % 4 == 3 && i % 8 == 3) {
            p2 = p0;
            p1 = p0;
        }
        const auto curve = [&](double t) {
            const double s = 1 - t;
            return p0 * (s * s) + p1 * (2 * s * t) + p2 * (t * t);
        };
        for (int sample = 0; sample < 8; ++sample) {
            const Vector2 p = point() * 1.5;
            const double expected = prism::test::curveDistance(p, curve);
            const double actual = quadraticBezier(p, p0, p1, p2);
            if (!std::isfinite(actual)) {
                return false;
            }
            worst = std::max(worst, std::abs(actual - expected) / std::max(1.0, scale));
        }
    }
    std::cout << "Bezier relative error: " << worst << '\n';
    return worst < 1e-6;
}

bool checkRoundArcs(prism::test::Pcg &random) {
    double worst = 0;
    for (int i = 0; i < 4000; ++i) {
        const Arc shape{{random.range(-50, 50), random.range(-50, 50)},
                        random.range(5, 200),
                        random.range(0, 20),
                        random.range(-4, 4),
                        i % 10 == 0 ? 1e-4 : random.range(1e-4, std::numbers::pi),
                        ArcCap::round};
        const auto centerline = [&](double t) {
            const double angle = shape.midAngle - shape.halfAperture + 2 * shape.halfAperture * t;
            return shape.center + Vector2{std::cos(angle), std::sin(angle)} * shape.radius;
        };
        for (int sample = 0; sample < 8; ++sample) {
            const Vector2 p =
                shape.center +
                Vector2{random.range(-1.5, 1.5), random.range(-1.5, 1.5)} * shape.radius;
            const double expected = prism::test::curveDistance(p, centerline) - shape.thickness / 2;
            const double actual = arc(p, shape);
            if (!std::isfinite(actual)) {
                return false;
            }
            worst = std::max(worst, std::abs(actual - expected) / shape.radius);
        }
    }
    std::cout << "Arc relative error: " << worst << '\n';
    return worst < 1e-6;
}

bool checkSuperellipses(prism::test::Pcg &random) {
    double worstDistance = 0;
    double worstGradient = 0;
    for (int i = 0; i < 2000; ++i) {
        const double power = random.range(2, 16);
        const double major = random.range(20, 200);
        const Vector2 half = random.unit() < 0.5 ? Vector2{major, major / random.range(1, 10)}
                                                 : Vector2{major / random.range(1, 10), major};
        const auto boundary = [&](double t) {
            const double angle = t * std::numbers::pi / 2;
            return Vector2{half.x * std::pow(std::cos(angle), 2 / power),
                           half.y * std::pow(std::sin(angle), 2 / power)};
        };
        for (int sample = 0; sample < 8; ++sample) {
            const double angle = random.range(0, std::numbers::pi / 2);
            const Vector2 onEdge = boundary(angle / (std::numbers::pi / 2));
            const Vector2 p = onEdge + Vector2{random.range(-2, 2), random.range(-2, 2)};
            if (p.x < 0 || p.y < 0) {
                continue;
            }
            const double cut = std::pow(0.5, 1 / power);
            const auto lowerPiece = [&](double t) {
                const double x = t * half.x * cut;
                return Vector2{x, half.y * std::pow(1 - std::pow(x / half.x, power), 1 / power)};
            };
            const auto upperPiece = [&](double t) {
                const double y = t * half.y * cut;
                return Vector2{half.x * std::pow(1 - std::pow(y / half.y, power), 1 / power), y};
            };
            const double unsignedDistance = std::min(prism::test::curveDistance(p, lowerPiece),
                                                     prism::test::curveDistance(p, upperPiece));
            const bool inside = std::pow(p.x / half.x, power) + std::pow(p.y / half.y, power) < 1;
            const double expected = inside ? -unsignedDistance : unsignedDistance;
            if (std::abs(expected) > 2) {
                continue;
            }
            const double actual = superellipse(p, half, power);
            constexpr double h = 1e-5;
            const Vector2 gradient{(superellipse(p + Vector2{h, 0}, half, power) -
                                    superellipse(p - Vector2{h, 0}, half, power)) /
                                       (2 * h),
                                   (superellipse(p + Vector2{0, h}, half, power) -
                                    superellipse(p - Vector2{0, h}, half, power)) /
                                       (2 * h)};
            worstDistance = std::max(worstDistance, std::abs(actual - expected));
            worstGradient = std::max(worstGradient, std::abs(length(gradient) - 1));
        }
    }
    std::cout << "Superellipse errors: " << worstDistance << ' ' << worstGradient << '\n';
    return worstDistance < 1e-4 && worstGradient < 1e-3;
}

bool checkDegenerateCurves() {
    const Arc tiny{{0, 0}, 10, 2, 0.5, 1e-4, ArcCap::butt};
    const Arc square{{0, 0}, 10, 2, 0.5, 1, ArcCap::square};
    const Arc empty{{0, 0}, 0, 0, 0, 1, ArcCap::round};
    const Vector2 beyond{10 * std::cos(0.5 + 1.2), 10 * std::sin(0.5 + 1.2)};
    return std::isfinite(arc({1, 1}, tiny)) && std::isfinite(arc({1, 1}, square)) &&
           std::isfinite(arc({0, 0}, empty)) &&
           arc(beyond, Arc{{0, 0}, 10, 2, 0.5, 1, ArcCap::butt}) > 0 &&
           std::isfinite(quadraticBezier({1, 1}, {0, 0}, {0, 0}, {0, 0})) &&
           std::isfinite(superellipse({0, 0}, {10, 5}, 4)) && superellipse({0, 0}, {10, 5}, 4) < 0;
}
}

int main() {
    prism::test::Pcg random(17);
    const bool passed = checkDegenerateCurves() && checkBeziers(random) && checkRoundArcs(random) &&
                        checkSuperellipses(random);
    prism::test::AllocGuard guard;
    const double guarded = quadraticBezier({1, 2}, {0, 0}, {4, 5}, {8, 0}) +
                           superellipse({3, 1}, {10, 6}, 5) +
                           arc({3, 3}, Arc{{0, 0}, 5, 1, 0, 1, ArcCap::round});
    if (!passed || !guard.counts().empty() || !std::isfinite(guarded)) {
        std::cerr << "Curve distance contract failed\n";
        return 1;
    }
    return 0;
}
