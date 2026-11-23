#include "telemetry/HalfBand.h"

#include <cmath>
#include <cstddef>
#include <numbers>

namespace prism::telemetry {
namespace {
constexpr double kaiserBeta = 10.0;

double besselI0(double x) noexcept {
    double sum = 1;
    double term = 1;
    for (int k = 1; k < 64; ++k) {
        term *= (x / (2 * k)) * (x / (2 * k));
        sum += term;
    }
    return sum;
}
}

std::array<double, halfBandTaps> designHalfBand() noexcept {
    std::array<double, halfBandTaps> taps{};
    constexpr int centre = static_cast<int>(halfBandTaps / 2);
    const double norm = besselI0(kaiserBeta);
    double sum = 0;

    for (int n = 0; n < static_cast<int>(halfBandTaps); ++n) {
        const int k = n - centre;
        const double ratio = static_cast<double>(k) / centre;
        const double window = besselI0(kaiserBeta * std::sqrt(1 - ratio * ratio)) / norm;
        const double ideal = k == 0 ? 0.5
                                    : std::sin(std::numbers::pi * k / 2) /
                                          (std::numbers::pi * static_cast<double>(k));
        taps[static_cast<std::size_t>(n)] = k != 0 && k % 2 == 0 ? 0.0 : ideal * window;
        sum += taps[static_cast<std::size_t>(n)];
    }

    for (double &tap : taps) {
        tap /= sum;
    }
    return taps;
}
}
