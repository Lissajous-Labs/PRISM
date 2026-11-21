#pragma once

#include "math/Vector2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <span>

namespace prism::math {
inline constexpr double wavetableTurn = 2 * std::numbers::pi;
[[nodiscard]] inline double wrapPhase(double phase) noexcept {
    return phase - std::floor(phase / wavetableTurn) * wavetableTurn;
}
[[nodiscard]] inline double cubicSample(double a, double b, double c, double d, double t) noexcept {
    return b + 0.5 * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a)));
}
struct WavetableView {
    std::span<const float> samples;
    std::size_t frames;
    std::size_t period;
    [[nodiscard]] double sample(double phase, double morph) const noexcept {
        if (frames == 0 || period == 0 || samples.size() / period < frames) {
            return 0;
        }
        const double u = std::clamp(morph, 0.0, 1.0) * static_cast<double>(frames - 1);
        const double v = wrapPhase(phase) / wavetableTurn * static_cast<double>(period);
        const auto fi = static_cast<std::ptrdiff_t>(std::floor(u));
        const auto pi = static_cast<std::ptrdiff_t>(std::floor(v));
        std::array<double, 4> rows{};
        for (std::ptrdiff_t a = -1; a <= 2; ++a) {
            const auto frame = static_cast<std::size_t>(
                std::clamp(fi + a, std::ptrdiff_t{0}, static_cast<std::ptrdiff_t>(frames - 1)));
            std::array<double, 4> values{};
            for (std::ptrdiff_t b = -1; b <= 2; ++b) {
                const auto n = static_cast<std::ptrdiff_t>(period);
                const auto index = static_cast<std::size_t>(((pi + b) % n + n) % n);
                values[static_cast<std::size_t>(b + 1)] = samples[frame * period + index];
            }
            rows[static_cast<std::size_t>(a + 1)] =
                cubicSample(values[0], values[1], values[2], values[3], v - std::floor(v));
        }
        return cubicSample(rows[0], rows[1], rows[2], rows[3], u - std::floor(u));
    }
};
[[nodiscard]] inline double quantizedWarp(double value, double levels, double sharpness) noexcept {
    const double x = value * levels;
    return (std::floor(x) + 0.5 +
            0.5 * std::tanh(sharpness * (x - std::floor(x) - 0.5)) / std::tanh(sharpness * 0.5)) /
           levels;
}
[[nodiscard]] inline double amplitudeWarp(double value, double phase, double amount,
                                          double harmonic, double offset = 0) noexcept {
    return value * (1 - amount + amount * std::cos(harmonic * phase + offset));
}
[[nodiscard]] inline double mirrorWarp(double phase, double amount) noexcept {
    const double u = wrapPhase(phase) / wavetableTurn;
    const double peak = 1 - 0.5 * std::clamp(amount, 0.0, 1.0);
    return wavetableTurn * (u <= peak ? u / peak : 1 - (u - peak) / (1 - peak));
}
[[nodiscard]] inline double bendWarp(double phase, double bend) noexcept {
    return wavetableTurn * std::pow(wrapPhase(phase) / wavetableTurn, std::exp2(bend));
}
[[nodiscard]] inline double syncWarp(double phase, double ratio) noexcept {
    return wrapPhase(phase * ratio);
}

struct WavetableCamera {
    double yaw = -std::numbers::pi / 5;
    double pitch = 28 * std::numbers::pi / 180;
    double inverseDistance = 0;
    double amplitudeScale = 0.58;
    double padding = 0.08;
    struct Point {
        double x, y, z;
    };
    [[nodiscard]] Point rotate(double x, double y, double z) const noexcept {
        const double cy = std::cos(yaw), sy = std::sin(yaw);
        const double cp = std::cos(pitch), sp = std::sin(pitch);
        const double xr = cy * x + sy * z;
        const double zr = -sy * x + cy * z;
        return {xr, cp * amplitudeScale * y - sp * zr, sp * amplitudeScale * y + cp * zr};
    }
    [[nodiscard]] Vector2 project(double x, double y, double z) const noexcept {
        const auto p = rotate(x, y, z);
        const double denominator = 1 - p.z * inverseDistance;
        return {p.x / denominator, p.y / denominator};
    }
    [[nodiscard]] double fit(double width, double height) const noexcept {
        double ux = 0, uy = 0;
        for (int i = 0; i < 8; ++i) {
            const auto p = project((i & 1) ? 1 : -1, (i & 2) ? 1 : -1, (i & 4) ? 1 : -1);
            ux = std::max(ux, std::abs(p.x));
            uy = std::max(uy, std::abs(p.y));
        }
        return (1 - 2 * padding) * std::min(width / (2 * ux), height / (2 * uy));
    }
    [[nodiscard]] bool descendingSlices() const noexcept {
        return std::cos(pitch) * std::cos(yaw) >= 0;
    }
};
}
