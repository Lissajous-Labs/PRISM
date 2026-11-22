#pragma once

#include "SdfShapes.h"
#include "Vector2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace prism::math {
enum class ArcCap { round, butt, square };

struct Arc {
    Vector2 center;
    double radius;
    double thickness;
    double midAngle;
    double halfAperture;
    ArcCap cap;
};

struct ArcSample {
    double distance;
    Vector2 gradient;
};

namespace detail {
[[nodiscard]] inline Vector2 bezierPoint(Vector2 p0, Vector2 p1, Vector2 p2, double t) noexcept {
    const double s = 1 - t;
    return p0 * (s * s) + p1 * (2 * s * t) + p2 * (t * t);
}

[[nodiscard]] inline double refineBezier(Vector2 p, Vector2 p0, Vector2 p1, Vector2 p2,
                                         double t) noexcept {
    const Vector2 a = p1 - p0;
    const Vector2 b = p0 - p1 * 2 + p2;
    for (int i = 0; i < 4; ++i) {
        const Vector2 offset = bezierPoint(p0, p1, p2, t) - p;
        const Vector2 tangent = (a + b * t) * 2;
        const double slope = dot(tangent, tangent) + dot(offset, b * 2);
        if (!(slope > 0)) {
            break;
        }
        t = std::clamp(t - dot(offset, tangent) / slope, 0.0, 1.0);
    }
    return t;
}
}

[[nodiscard]] inline double quadraticBezier(Vector2 p, Vector2 p0, Vector2 p1,
                                            Vector2 p2) noexcept {
    const Vector2 a = p1 - p0;
    const Vector2 b = p0 - p1 * 2 + p2;
    const Vector2 d = p0 - p;
    const double curvature = dot(b, b);
    const double speed = dot(a, a);
    std::array<double, 5> candidates{0, 1, 0, 0, 0};
    std::size_t count = 2;
    if (curvature <= 1e-6 * std::max(speed, 1e-6)) {
        const Vector2 chord = p2 - p0;
        const double t =
            std::clamp(dot(p - p0, chord) / std::max(dot(chord, chord), 1e-24), 0.0, 1.0);
        candidates[count++] = detail::refineBezier(p, p0, p1, p2, t);
    } else {
        const double kx = dot(a, b) / curvature;
        const double ky = (2 * speed + dot(d, b)) / (3 * curvature);
        const double kz = dot(d, a) / curvature;
        const double pc = ky - kx * kx;
        const double qc = kx * (2 * kx * kx - 3 * ky) + kz;
        const double discriminant = qc * qc + 4 * pc * pc * pc;
        if (discriminant >= 0) {
            const double root = std::sqrt(discriminant);
            candidates[count++] = std::cbrt((root - qc) / 2) + std::cbrt((-root - qc) / 2) - kx;
        } else {
            const double z = std::sqrt(-pc);
            const double theta = std::acos(std::clamp(qc / (2 * pc * z), -1.0, 1.0)) / 3;
            for (int k = 0; k < 3; ++k) {
                candidates[count++] = 2 * z * std::cos(theta - 2 * std::numbers::pi * k / 3) - kx;
            }
        }
    }
    double best = length(p0 - p);
    for (std::size_t i = 0; i < count; ++i) {
        const double t = detail::refineBezier(p, p0, p1, p2, std::clamp(candidates[i], 0.0, 1.0));
        best = std::min(best, length(detail::bezierPoint(p0, p1, p2, t) - p));
    }
    return best;
}

[[nodiscard]] inline ArcSample sampleArc(Vector2 p, const Arc &shape) noexcept {
    const double turn = std::numbers::pi / 2 - shape.midAngle;
    const double cosine = std::cos(turn);
    const double sine = std::sin(turn);
    const Vector2 local = p - shape.center;
    const double foldedX = cosine * local.x - sine * local.y;
    const Vector2 q{std::abs(foldedX), sine * local.x + cosine * local.y};
    const Vector2 end{std::sin(shape.halfAperture), std::cos(shape.halfAperture)};
    const double halfWidth = shape.thickness / 2;
    ArcSample result;
    if (end.y * q.x <= end.x * q.y) {
        const double norm = length(q);
        const double radial = norm - shape.radius;
        result.distance = std::abs(radial) - halfWidth;
        const double side = radial > 0 ? 1 : (radial < 0 ? -1 : 0);
        result.gradient = norm > 0 ? q * (side / norm) : Vector2{};
    } else if (shape.cap == ArcCap::butt or shape.cap == ArcCap::square) {
        const Vector2 frame{q.x * end.y - q.y * end.x, q.x * end.x + q.y * end.y - shape.radius};
        const Vector2 half{shape.cap == ArcCap::butt ? 0 : halfWidth, halfWidth};
        result.distance = box(frame, half);
        const Vector2 capGradient = roundedRectGradient(frame, half, {});
        result.gradient = {end.y * capGradient.x + end.x * capGradient.y,
                           -end.x * capGradient.x + end.y * capGradient.y};
    } else {
        const Vector2 delta = q - end * shape.radius;
        const double norm = length(delta);
        result.distance = norm - halfWidth;
        result.gradient = norm > 0 ? delta * (1 / norm) : Vector2{};
    }
    result.gradient.x *= foldedX > 0 ? 1 : (foldedX < 0 ? -1 : 0);
    result.gradient = {cosine * result.gradient.x + sine * result.gradient.y,
                       -sine * result.gradient.x + cosine * result.gradient.y};
    return result;
}

[[nodiscard]] inline double arc(Vector2 p, const Arc &shape) noexcept {
    return sampleArc(p, shape).distance;
}
}
