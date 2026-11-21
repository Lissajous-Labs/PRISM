#pragma once

#include <algorithm>
#include <cmath>

namespace prism::math {
struct BallisticsSettings {
    double attack = 0.008;
    double release = 0.095;
    double hold = 0.45;
    double gravity = 1.85;
    double fluxScale = 0.18;
    double fluxTime = 0.14;
};

struct SpectralBallistics {
    double height = 0;
    double peak = 0;
    double velocity = 0;
    double holdRemaining = 0;
    double flux = 0;

    void step(double input, double dt, const BallisticsSettings &s = {}) noexcept {
        if (!(dt > 0)) {
            return;
        }
        const double x = std::clamp(input, 0.0, 1.0);
        const double initial = height;
        const double tau = x >= initial ? s.attack : s.release;
        const auto h = [=](double t) { return x + (initial - x) * std::exp(-t / tau); };
        const double difference = std::max(0.0, x - initial) / s.fluxScale;
        const double capped = difference > 1 ? std::min(dt, tau * std::log(difference)) : 0;
        if (capped > 0) {
            flux = 1 + (flux - 1) * std::exp(-capped / s.fluxTime);
        }
        const double duration = dt - capped;
        if (duration > 0) {
            const double f0 = difference * std::exp(-capped / tau);
            const double decay = std::exp(-duration / s.fluxTime);
            const double rate = 1 / s.fluxTime - 1 / tau;
            const double convolution =
                std::abs(rate) < 1e-12 ? duration * decay / s.fluxTime
                                       : (std::exp(-duration / tau) - decay) / (s.fluxTime * rate);
            flux = flux * decay + f0 * convolution;
        }
        height = h(dt);
        if (x >= initial && height >= peak) {
            peak = height;
            velocity = 0;
            holdRemaining = s.hold;
            return;
        }
        double elapsed = 0;
        while (elapsed < dt) {
            const double held = std::min(holdRemaining, dt - elapsed);
            holdRemaining -= held;
            elapsed += held;
            if (elapsed >= dt) {
                break;
            }
            const double remaining = dt - elapsed;
            const auto p = [&](double t) { return peak - velocity * t - 0.5 * s.gravity * t * t; };
            if (p(remaining) >= h(dt)) {
                peak = p(remaining);
                velocity += s.gravity * remaining;
                break;
            }
            double lo = 0, hi = remaining;
            for (int i = 0; i < 48; ++i) {
                const double mid = (lo + hi) * 0.5;
                if (p(mid) > h(elapsed + mid)) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            elapsed += hi;
            peak = h(elapsed);
            velocity = 0;
            holdRemaining = s.hold;
            if (s.hold <= 0) {
                peak = height;
                break;
            }
        }
        peak = std::max(peak, height);
    }
    [[nodiscard]] bool active(double epsilon = 1e-4) const noexcept {
        return height > epsilon || peak > epsilon || flux > epsilon;
    }
};
}
