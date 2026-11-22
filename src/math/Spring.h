#pragma once

#include <cmath>
#include <limits>
#include <numbers>

namespace prism::math {
struct SpringState {
    double position;
    double velocity;
};

struct SpringParameters {
    double omega;
    double zeta = 1;
};

struct SpringPropagator {
    double a00;
    double a01;
    double a10;
    double a11;
};

[[nodiscard]] inline SpringPropagator springPropagator(SpringParameters spring,
                                                       double dt) noexcept {
    const double omega = spring.omega;
    const double zeta = spring.zeta;
    if (!(dt > 0)) {
        return {1, 0, 0, 1};
    }
    if (!std::isfinite(omega)) {
        return {0, 0, 0, 0};
    }
    const double x = omega * dt;
    const double difference = zeta - 1;
    if (std::abs(difference) <= 1e-9) {
        const double decay = std::exp(-x);
        return {decay * (1 + x), decay * dt, -decay * omega * omega * dt, decay * (1 - x)};
    }
    const double decay = std::exp(-zeta * x);
    if (zeta < 1) {
        const double damped = omega * std::sqrt((1 - zeta) * (1 + zeta));
        const double angle = damped * dt;
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        const double ratio = zeta * omega / damped;
        return {decay * (c + ratio * s), decay * s / damped, -decay * omega * omega * s / damped,
                decay * (c - ratio * s)};
    }
    const double root = omega * std::sqrt(difference * (zeta + 1));
    const double y = root * dt;
    const double sinhcY = y == 0 ? 1 : std::sinh(y) / y;
    const double c = std::cosh(y);
    const double s = root * dt * sinhcY;
    const double ratio = zeta * omega / root;
    return {decay * (c + ratio * s), decay * dt * sinhcY, -decay * omega * omega * dt * sinhcY,
            decay * (c - ratio * s)};
}

[[nodiscard]] inline SpringState springStep(SpringState state, double target,
                                            SpringParameters spring, double dt) noexcept {
    if (!std::isfinite(spring.omega) && dt > 0) {
        return {target, 0};
    }
    const SpringPropagator phi = springPropagator(spring, dt);
    const double offset = state.position - target;
    return {target + phi.a00 * offset + phi.a01 * state.velocity,
            phi.a10 * offset + phi.a11 * state.velocity};
}

[[nodiscard]] inline double lambertWMinusOne(double z) noexcept {
    constexpr double branch = -0.36787944117144233;
    if (!(z >= branch) || !(z < 0)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double p = -std::sqrt(2 * (std::numbers::e * z + 1));
    double w = z > -0.25 ? std::log(-z) - std::log(-std::log(-z)) : -1 + p - p * p / 3;
    for (int iteration = 0; iteration < 32; ++iteration) {
        const double e = std::exp(w);
        const double f = w * e - z;
        const double derivative = e * (w + 1);
        if (derivative == 0) {
            break;
        }
        const double step = f / (derivative - (w + 2) * f / (2 * w + 2));
        w -= step;
        if (std::abs(step) <= 1e-15 * std::abs(w)) {
            break;
        }
    }
    return w;
}

[[nodiscard]] inline double criticalSettlingTime(double omega, double tolerance) noexcept {
    if (!(omega > 0) || !(tolerance > 0) || !(tolerance < 1)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return (-lambertWMinusOne(-tolerance / std::numbers::e) - 1) / omega;
}
}
