#include "../support/AllocGuard.h"
#include "../support/Pcg.h"
#include "math/Sdf.h"

#include <cmath>
#include <functional>
#include <iostream>

namespace {
using namespace prism::math;
using Field = std::function<double(Vector2)>;

struct GradientResult {
    bool valid;
    int checked;
};

GradientResult checkGradient(const Field &field, Vector2 p) {
    constexpr double h = 1e-6;
    const double center = field(p);
    const Vector2 forward{(field(p + Vector2{h, 0}) - center) / h,
                          (field(p + Vector2{0, h}) - center) / h};
    const Vector2 backward{(center - field(p - Vector2{h, 0})) / h,
                           (center - field(p - Vector2{0, h})) / h};
    if (!std::isfinite(center) || length(forward - backward) > 1e-3) {
        return {std::isfinite(center), 0};
    }
    return {std::abs(length((forward + backward) * 0.5) - 1) <= 1e-3, 1};
}

bool fuzzShape(prism::test::Pcg &random, int shape) {
    const double scale = random.range(1, 200);
    const double aspect = random.range(1, 100);
    const Vector2 half{scale, scale / aspect};
    const Vector2 a{random.range(-scale, scale), random.range(-scale, scale)};
    const Vector2 b{random.range(-scale, scale), random.range(-scale, scale)};
    const Vector2 c{random.range(-scale, scale), random.range(-scale, scale)};
    const double turn = random.range(0, 6.283185307179586);
    const CornerRadii radii =
        clampRadii({random.range(0, scale), random.unit() < 0.2 ? 0 : random.range(0, scale),
                    random.range(0, scale), random.range(0, scale)},
                   half);
    const Field field = [&](Vector2 p) {
        switch (shape) {
        case 0:
            return roundedRect(p, half, radii);
        case 1:
            return capsule(p, a, b, half.y);
        case 2:
            return orientedBox(p, a, half, {std::cos(turn), std::sin(turn)});
        case 3:
            return triangle(p, a, b, c);
        default:
            return roundedTriangle(p, a, b, c, half.y);
        }
    };
    int checked = 0;
    for (int i = 0; i < 20; ++i) {
        const Vector2 p{random.range(-1.5 * scale, 1.5 * scale),
                        random.range(-1.5 * scale, 1.5 * scale)};
        const double distance = field(p);
        if (!std::isfinite(distance)) {
            return false;
        }
        if (std::abs(distance) > 0.25 * scale) {
            continue;
        }
        const auto result = checkGradient(field, p);
        if (!result.valid) {
            std::cerr << "Gradient failure for shape " << shape << " at distance " << distance
                      << '\n';
            return false;
        }
        checked += result.checked;
    }
    return checked >= 0;
}

bool checkKnownValues() {
    const CornerRadii radii{10, 0, 4, 2};
    const auto overlapping = clampRadii({40, 40, 40, 40}, {20, 10});
    const auto inner = innerBorder({20, 10}, {6, 6, 6, 6}, {2, 1, 4, 3});
    return std::abs(roundedRect({20, 0}, {20, 10}, radii)) < 1e-12 &&
           std::abs(roundedRect({20 - 10 + 10 * std::sqrt(0.5), -10 + 10 - 10 * std::sqrt(0.5)},
                                {20, 10}, radii)) < 1e-12 &&
           std::abs(overlapping.topLeft - 10) < 1e-12 &&
           std::abs(box({3, 4}, {0, 0}) - 5) < 1e-12 &&
           std::abs(capsule({0, 3}, {0, 0}, {0, 0}, 1) - 2) < 1e-12 &&
           std::abs(triangle({0, -1}, {-1, 0}, {1, 0}, {0, 1}) - 1) < 1e-12 &&
           triangle({0, 0.25}, {-1, 0}, {1, 0}, {0, 1}) < 0 &&
           std::abs(inner.center.x + 1) < 1e-12 && std::abs(inner.half.x - 17) < 1e-12 &&
           std::abs(inner.radii.topRight - 2) < 1e-12 &&
           std::isfinite(roundedTriangle({0, 0}, {1, 1}, {2, 2}, {3, 3}, 1));
}
}

int main() {
    prism::test::Pcg random(5);
    bool passed = checkKnownValues();
    for (int i = 0; passed && i < 25000; ++i) {
        passed = fuzzShape(random, i % 5);
    }
    prism::test::AllocGuard guard;
    const double guarded =
        roundedRect({1, 2}, {3, 4}, {1, 1, 1, 1}) + triangle({0, 0}, {1, 0}, {0, 1}, {1, 1});
    if (!passed || !guard.counts().empty() || !std::isfinite(guarded)) {
        std::cerr << "Shape distance contract failed\n";
        return 1;
    }
    return 0;
}
