#pragma once

#include "TrigPoly.h"

#include <cmath>
#include <optional>

namespace prism::math {
struct Vector2 {
    double x;
    double y;
};

struct Point2 {
    double x;
    double y;
};

struct SingularValues {
    double maximum;
    double minimum;
};

struct Affine2D {
    double a = 1;
    double b = 0;
    double c = 0;
    double d = 1;
    double tx = 0;
    double ty = 0;

    [[nodiscard]] static constexpr Affine2D translation(double x, double y) noexcept {
        return {1, 0, 0, 1, x, y};
    }

    [[nodiscard]] static constexpr Affine2D scale(double x, double y) noexcept {
        return {x, 0, 0, y, 0, 0};
    }

    [[nodiscard]] static constexpr Affine2D rotation(double radians) noexcept {
        const auto [sine, cosine] = sinCos(radians);
        return {cosine, sine, -sine, cosine, 0, 0};
    }

    [[nodiscard]] constexpr Point2 apply(Point2 point) const noexcept {
        return {a * point.x + c * point.y + tx, b * point.x + d * point.y + ty};
    }

    [[nodiscard]] constexpr Vector2 apply(Vector2 vector) const noexcept {
        return {a * vector.x + c * vector.y, b * vector.x + d * vector.y};
    }

    [[nodiscard]] constexpr double determinant() const noexcept { return a * d - b * c; }

    [[nodiscard]] constexpr std::optional<Affine2D> inverse() const noexcept {
        const double det = determinant();
        if (!(det != 0) || !std::isfinite(1 / det) || !std::isfinite(tx) || !std::isfinite(ty)) {
            return std::nullopt;
        }
        const double ia = d / det;
        const double ib = -b / det;
        const double ic = -c / det;
        const double id = a / det;
        return Affine2D{ia, ib, ic, id, -(ia * tx + ic * ty), -(ib * tx + id * ty)};
    }

    [[nodiscard]] constexpr std::optional<Vector2>
    pullBackGradient(Vector2 gradient) const noexcept {
        const auto inverted = inverse();
        if (!inverted) {
            return std::nullopt;
        }
        return Vector2{inverted->a * gradient.x + inverted->b * gradient.y,
                       inverted->c * gradient.x + inverted->d * gradient.y};
    }

    [[nodiscard]] SingularValues singularValues() const noexcept {
        const double energy = (a * a + b * b + c * c + d * d) / 2;
        const double spread = std::hypot((a * a + b * b - c * c - d * d) / 2, a * c + b * d);
        const double maximum = std::sqrt(energy + spread);
        return {maximum, maximum == 0 ? 0 : std::abs(determinant()) / maximum};
    }

    friend constexpr Affine2D operator*(const Affine2D &left, const Affine2D &right) noexcept {
        return {left.a * right.a + left.c * right.b,
                left.b * right.a + left.d * right.b,
                left.a * right.c + left.c * right.d,
                left.b * right.c + left.d * right.d,
                left.a * right.tx + left.c * right.ty + left.tx,
                left.b * right.tx + left.d * right.ty + left.ty};
    }
};
}
