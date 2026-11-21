#include "../support/AllocGuard.h"
#include "math/Ballistics.h"

#include <array>
#include <cmath>
#include <iostream>

namespace {
using prism::math::SpectralBallistics;
std::array<SpectralBallistics, 240> trajectory(int rate) {
    SpectralBallistics state;
    std::array<SpectralBallistics, 240> result{};
    const int subdivision = rate / 60;
    for (std::size_t frame = 0; frame < result.size(); ++frame) {
        const double input = frame < 30 ? 0.8 : frame < 120 ? 0.0 : frame < 150 ? 0.6 : 0.1;
        for (int i = 0; i < subdivision; ++i) {
            state.step(input, 1.0 / rate);
        }
        result[frame] = state;
    }
    return result;
}
}
int main() {
    const auto reference = trajectory(60);
    for (int rate : {120, 240}) {
        const auto candidate = trajectory(rate);
        for (std::size_t i = 0; i < reference.size(); ++i) {
            const auto &a = reference[i];
            const auto &b = candidate[i];
            const double error =
                std::max({std::abs(a.height - b.height), std::abs(a.peak - b.peak),
                          std::abs(a.flux - b.flux), std::abs(a.velocity - b.velocity),
                          std::abs(a.holdRemaining - b.holdRemaining)});
            if (error > 1e-5 || b.peak < b.height || !std::isfinite(b.flux)) {
                std::cerr << "Ballistics error " << error << " at " << rate << " Hz frame " << i
                          << '\n';
                return 1;
            }
        }
    }
    SpectralBallistics state;
    {
        prism::test::AllocGuard guard;
        for (int i = 0; i < 10000; ++i) {
            state.step((i % 240) < 60 ? 0.9 : 0, 1.0 / 240);
        }
    }
    std::cout << "60/120/240 Hz height, gravity peaks, hold and flux agree within 1e-5\n";
}
