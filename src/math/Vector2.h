#pragma once

#include <cmath>

namespace prism::math {
struct Vector2 {
    double x;
    double y;

    friend constexpr Vector2 operator+(Vector2 left, Vector2 right) noexcept {
        return {left.x + right.x, left.y + right.y};
    }
    friend constexpr Vector2 operator-(Vector2 left, Vector2 right) noexcept {
        return {left.x - right.x, left.y - right.y};
    }
    friend constexpr Vector2 operator-(Vector2 value) noexcept { return {-value.x, -value.y}; }
    friend constexpr Vector2 operator*(Vector2 value, double scale) noexcept {
        return {value.x * scale, value.y * scale};
    }
    friend constexpr Vector2 operator*(double scale, Vector2 value) noexcept {
        return value * scale;
    }
    friend constexpr bool operator==(Vector2, Vector2) = default;
};

struct Point2 {
    double x;
    double y;
};

[[nodiscard]] constexpr double dot(Vector2 left, Vector2 right) noexcept {
    return left.x * right.x + left.y * right.y;
}

[[nodiscard]] constexpr double cross(Vector2 left, Vector2 right) noexcept {
    return left.x * right.y - left.y * right.x;
}

[[nodiscard]] inline double length(Vector2 value) noexcept { return std::hypot(value.x, value.y); }

[[nodiscard]] constexpr Vector2 absolute(Vector2 value) noexcept {
    return {value.x < 0 ? -value.x : value.x, value.y < 0 ? -value.y : value.y};
}

[[nodiscard]] constexpr Vector2 maximum(Vector2 value, double floor) noexcept {
    return {value.x > floor ? value.x : floor, value.y > floor ? value.y : floor};
}
}
