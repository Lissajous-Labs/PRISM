#include "../support/AllocGuard.h"
#include "math/Affine2D.h"
#include "math/PixelSnap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {
using namespace prism::math;

constexpr Affine2D compileTime = Affine2D::translation(3, -2) * Affine2D::scale(2, 4);
static_assert(compileTime.apply(Point2{1, 1}).x == 5 && compileTime.apply(Point2{1, 1}).y == 2);
static_assert(!Affine2D::scale(0, 1).inverse());

double unit(std::uint64_t &state) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<double>(state >> 11) * 0x1p-53;
}

bool near(double actual, double expected, double tolerance) {
    return std::abs(actual - expected) <= tolerance * std::max(1.0, std::abs(expected));
}

bool checkAffine() {
    std::uint64_t state = 31;
    for (int i = 0; i < 10000; ++i) {
        const Affine2D matrix{4 * unit(state) - 2,     4 * unit(state) - 2,
                              4 * unit(state) - 2,     4 * unit(state) - 2,
                              200 * unit(state) - 100, 200 * unit(state) - 100};
        const auto inverse = matrix.inverse();
        if (std::abs(matrix.determinant()) < 1e-3) {
            continue;
        }
        if (!inverse) {
            return false;
        }
        const Point2 point{50 * unit(state) - 25, 50 * unit(state) - 25};
        const auto restored = inverse->apply(matrix.apply(point));
        const auto identity = *inverse * matrix;
        const auto values = matrix.singularValues();
        const double frobenius =
            matrix.a * matrix.a + matrix.b * matrix.b + matrix.c * matrix.c + matrix.d * matrix.d;
        const Vector2 normal{unit(state) - 0.5, unit(state) - 0.5};
        const auto gradient = matrix.pullBackGradient(normal);
        const Point2 physical = matrix.apply(point);
        const double step = 1e-4;
        const auto field = [&](Point2 sample) {
            const auto local = inverse->apply(sample);
            return normal.x * local.x + normal.y * local.y;
        };
        const double dx =
            (field({physical.x + step, physical.y}) - field({physical.x - step, physical.y})) /
            (2 * step);
        const double dy =
            (field({physical.x, physical.y + step}) - field({physical.x, physical.y - step})) /
            (2 * step);
        if (!near(restored.x, point.x, 1e-9) || !near(restored.y, point.y, 1e-9) ||
            !near(identity.a, 1, 1e-9) || !near(identity.c, 0, 1e-9) ||
            !near(identity.tx, 0, 1e-9) ||
            !near(values.maximum * values.minimum, std::abs(matrix.determinant()), 1e-12) ||
            !near(values.maximum * values.maximum + values.minimum * values.minimum, frobenius,
                  1e-12) ||
            values.minimum > values.maximum || !gradient || !near(gradient->x, dx, 1e-5) ||
            !near(gradient->y, dy, 1e-5)) {
            return false;
        }
    }
    const auto tiny = Affine2D::scale(1e8, 1e-8).singularValues();
    const auto rotated = (Affine2D::scale(1.5, 1.5) * Affine2D::rotation(0.7)).singularValues();
    const auto sheared = Affine2D{1, 0, 3, 1, 0, 0}.singularValues();
    return near(tiny.maximum, 1e8, 1e-15) && near(tiny.minimum, 1e-8, 1e-15) &&
           near(rotated.maximum, 1.5, 1e-6) && near(rotated.minimum, 1.5, 1e-6) &&
           near(sheared.maximum, (3 + std::sqrt(13.0)) / 2, 1e-14) &&
           near(sheared.minimum, (std::sqrt(13.0) - 3) / 2, 1e-14) &&
           !Affine2D{1, 2, 2, 4, 0, 0}.pullBackGradient({1, 0}) &&
           !Affine2D{1, 0, 0, 1, std::numeric_limits<double>::infinity(), 0}.inverse();
}

bool checkSnapping() {
    std::array<double, 25> scales{};
    for (std::size_t i = 0; i < scales.size(); ++i) {
        scales[i] = 1 + static_cast<double>(i) / 12;
    }
    std::uint64_t state = 47;
    std::vector<double> boundaries;
    std::vector<std::int64_t> snapped;
    boundaries.reserve(33);
    snapped.reserve(33);
    for (int test = 0; test < 10000; ++test) {
        const double scale = scales[static_cast<std::size_t>(test) % scales.size()];
        const auto count = 2 + static_cast<std::size_t>(unit(state) * 32);
        boundaries.assign(1, 1000 * unit(state) - 500);
        for (std::size_t i = 1; i < count; ++i) {
            const double kind = unit(state);
            const double width = kind < 0.1   ? 0
                                 : kind < 0.2 ? 0.01 * unit(state)
                                              : 40 * unit(state);
            boundaries.push_back(boundaries.back() + width);
        }
        snapped.assign(count, 0);
        if (!snapPartition(boundaries, scale, snapped)) {
            return false;
        }
        std::int64_t total = 0;
        for (std::size_t i = 1; i < count; ++i) {
            const auto width = snapped[i] - snapped[i - 1];
            const double exact = scale * boundaries[i] - scale * boundaries[i - 1];
            total += width;
            if (width < 0 || std::abs(static_cast<double>(width) - exact) >= 1 ||
                (exact >= 1 && width < 1)) {
                return false;
            }
        }
        const std::size_t middle = count / 2;
        const auto parent = snapBoundary(boundaries[middle], scale);
        if (total != snapped.back() - snapped.front() || !parent || *parent != snapped[middle]) {
            return false;
        }
        const auto stroke = snapStroke(200 * unit(state) - 100, 4 * unit(state) + 1e-6, scale);
        if (!stroke) {
            return false;
        }
        const double half = static_cast<double>(stroke->width) / 2;
        if (stroke->width < 1 || std::floor(stroke->center - half) != stroke->center - half ||
            std::floor(stroke->center + half) != stroke->center + half) {
            return false;
        }
    }
    const std::array<double, 2> descending{1, 0};
    std::array<std::int64_t, 2> output{};
    return snapBoundary(-0.5, 1) == 0 && snapBoundary(0.5, 1) == 1 &&
           snapStroke(10.2, 1, 1.25)->center == 12.5 && snapStroke(10.2, 1.6, 1.25)->center == 13 &&
           !snapPartition(descending, 1, output) && !snapBoundary(1, 0) &&
           !snapBoundary(std::numeric_limits<double>::infinity(), 1) && !snapBoundary(1e300, 1) &&
           !snapStroke(0, 0, 1);
}
}

int main() {
    const bool passed = checkAffine() && checkSnapping();
    prism::test::AllocGuard guard;
    std::array<double, 3> boundaries{0, 1.3, 2.6};
    std::array<std::int64_t, 3> snapped{};
    const bool guarded =
        snapPartition(boundaries, 1.25, snapped) && Affine2D::rotation(0.25).inverse().has_value();
    if (!passed || !guarded || !guard.counts().empty()) {
        std::cerr << "Geometry contract failed\n";
        return 1;
    }
    return 0;
}
