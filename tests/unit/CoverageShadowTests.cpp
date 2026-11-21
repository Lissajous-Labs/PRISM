#include "../support/AllocGuard.h"
#include "../support/Pcg.h"
#include "math/Coverage.h"
#include "math/ShadowMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

namespace {
using namespace prism::math;

double halfPlaneArea(Vector2 normal, double offset) {
    std::vector<Vector2> polygon{{-0.5, -0.5}, {0.5, -0.5}, {0.5, 0.5}, {-0.5, 0.5}};
    std::vector<Vector2> clipped;
    const auto side = [&](Vector2 p) { return dot(normal, p) + offset; };
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Vector2 current = polygon[i];
        const Vector2 next = polygon[(i + 1) % polygon.size()];
        const double a = side(current);
        const double b = side(next);
        if (a <= 0) {
            clipped.push_back(current);
        }
        if ((a < 0) != (b < 0) && a != b) {
            clipped.push_back(current + (next - current) * (a / (a - b)));
        }
    }
    double area = 0;
    for (std::size_t i = 0; i < clipped.size(); ++i) {
        area += cross(clipped[i], clipped[(i + 1) % clipped.size()]);
    }
    return std::abs(area) / 2;
}

bool checkCoverage(prism::test::Pcg &random) {
    double worst = 0;
    for (int i = 0; i < 100000; ++i) {
        const double angle = random.range(0, 2 * std::numbers::pi);
        const Vector2 normal{std::cos(angle), std::sin(angle)};
        const double distance = random.range(-1, 1);
        worst = std::max(worst,
                         std::abs(boxCoverage(distance, normal) - halfPlaneArea(normal, distance)));
        if (std::abs(boxCoverage(-distance, normal) - (1 - boxCoverage(distance, normal))) >
            1e-12) {
            return false;
        }
    }
    std::cout << "Box coverage error: " << worst << '\n';
    return worst < 1e-12 && std::abs(boxCoverage(0.2, {1, 0}) - 0.3) < 1e-15 &&
           std::abs(gaussianCoverage(0) - 0.5) < 1e-7 &&
           std::abs(gaussianCoverage(0.3) - 0.5 * std::erfc(0.3 / (std::numbers::sqrt2 * 0.45))) <
               2e-7 &&
           borderWeights(0.5, 0.25).border == 0.375;
}

double referenceSlice(double y, Vector2 half, double radius) {
    const double corner = std::min(half.y - radius - std::abs(y), 0.0);
    return half.x - radius + std::sqrt(std::max(0.0, radius * radius - corner * corner));
}

double referenceIntegrand(Vector2 p, Vector2 half, double radius, double sigma, double y) {
    const double scale = 1 / (std::numbers::sqrt2 * sigma);
    const double slice = referenceSlice(y, half, radius);
    const double inner = 0.5 * (std::erf((p.x + slice) * scale) - std::erf((p.x - slice) * scale));
    const double offset = p.y - y;
    return inner * std::exp(-offset * offset / (2 * sigma * sigma)) /
           (std::sqrt(2 * std::numbers::pi) * sigma);
}

template <class Function>
double simpson(Function &&f, double a, double b, double fa, double fm, double fb, double whole,
               int depth) {
    const double middle = (a + b) / 2;
    const double left = (a + middle) / 2;
    const double right = (middle + b) / 2;
    const double fl = f(left);
    const double fr = f(right);
    const double leftArea = (middle - a) * (fa + 4 * fl + fm) / 6;
    const double rightArea = (b - middle) * (fm + 4 * fr + fb) / 6;
    if (depth <= 0 || std::abs(leftArea + rightArea - whole) < 1e-12) {
        return leftArea + rightArea;
    }
    return simpson(f, a, middle, fa, fl, fm, leftArea, depth - 1) +
           simpson(f, middle, b, fm, fr, fb, rightArea, depth - 1);
}

double referenceShadow(Vector2 p, Vector2 half, double radius, double sigma) {
    const auto f = [&](double y) { return referenceIntegrand(p, half, radius, sigma, y); };
    const std::array<double, 4> breaks{-half.y, -(half.y - radius), half.y - radius, half.y};
    double total = 0;
    for (std::size_t i = 0; i + 1 < breaks.size(); ++i) {
        const int pieces = 1024;
        const double width = (breaks[i + 1] - breaks[i]) / pieces;
        for (int piece = 0; piece < pieces; ++piece) {
            const double a = breaks[i] + piece * width;
            const double b = a + width;
            const double fa = f(a);
            const double fm = f((a + b) / 2);
            const double fb = f(b);
            total += simpson(f, a, b, fa, fm, fb, width * (fa + 4 * fm + fb) / 6, 8);
        }
    }
    return total;
}

bool checkShadows(prism::test::Pcg &random) {
    double erfError = 0;
    for (int i = 0; i <= 10000; ++i) {
        const double x = -4 + 8.0 * i / 10000;
        erfError = std::max(erfError, std::abs(erfApprox(x) - std::erf(x)));
    }
    double worst = 0;
    for (int i = 0; i < 300; ++i) {
        const double radius = random.range(1, 40);
        const double ratio = std::pow(10.0, random.range(-1, 2));
        const double sigma = std::min(radius * ratio, 400.0);
        const Vector2 half{random.range(radius, radius + 100), random.range(radius, radius + 100)};
        const Vector2 p{random.range(-1.5, 1.5) * (half.x + 3 * sigma),
                        random.range(-1.5, 1.5) * (half.y + 3 * sigma)};
        worst = std::max(worst, std::abs(roundedBoxShadow(p, half, radius, sigma) -
                                         referenceShadow(p, half, radius, sigma)));
    }
    const double exactBox =
        0.25 *
        (std::erf(3 / (std::numbers::sqrt2 * 2)) - std::erf(-1 / (std::numbers::sqrt2 * 2))) *
        (std::erf(5 / (std::numbers::sqrt2 * 2)) - std::erf(1 / (std::numbers::sqrt2 * 2)));
    std::cout << "Shadow errors: " << erfError << ' ' << worst << '\n';
    return erfError < 1.5e-7 && worst < 1e-4 &&
           std::abs(boxShadow({1, 3}, {2, 2}, 2) - exactBox) < 1e-6 &&
           boxShadow({0, 0}, {1, 1}, 0) == 1 && roundedBoxShadow({5, 5}, {1, 1}, 0.5, 0) == 0;
}
}

int main() {
    prism::test::Pcg random(29);
    const bool passed = checkCoverage(random) && checkShadows(random);
    prism::test::AllocGuard guard;
    const double guarded = boxCoverage(0.1, {0.6, 0.8}) + roundedBoxShadow({1, 1}, {10, 5}, 3, 2);
    if (!passed || !guard.counts().empty() || !std::isfinite(guarded)) {
        std::cerr << "Coverage and shadow contract failed\n";
        return 1;
    }
    return 0;
}
