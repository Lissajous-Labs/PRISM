#include "../support/AllocGuard.h"
#include "math/TrigPoly.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>

namespace {
using V = prism::math::SimdVec<float, 4>;
constexpr V inputs(std::array<float, 4>{-6234.5f, -0.0f, 1.125f, 6234.5f});
constexpr auto compileTime = prism::math::sinCos(inputs);
constexpr auto compileAcos = prism::math::acos(V(std::array<float, 4>{-1, -0.5f, 0.5f, 1}));
static_assert(prism::math::sinCos(0.0f).sin == 0);
static_assert(prism::math::acos(1.0f) == 0);

bool check() {
    double maxSin = 0;
    double maxCos = 0;
    double maxAcos = 0;
    for (int i = 0; i <= 100000; i += 4) {
        V angles;
        V unit;
        for (int lane = 0; lane < 4; ++lane) {
            const double fraction = static_cast<double>(std::min(i + lane, 100000)) / 100000;
            angles[lane] = static_cast<float>((2 * fraction - 1) * 2000 * std::numbers::pi);
            unit[lane] = static_cast<float>(2 * fraction - 1);
        }
        const auto result = prism::math::sinCos(angles);
        const auto inverse = prism::math::acos(unit);
        for (int lane = 0; lane < 4; ++lane) {
            const auto scalar = prism::math::sinCos(angles[lane]);
            if (std::bit_cast<std::uint32_t>(scalar.sin) !=
                    std::bit_cast<std::uint32_t>(result.sin[lane]) ||
                std::bit_cast<std::uint32_t>(scalar.cos) !=
                    std::bit_cast<std::uint32_t>(result.cos[lane]) ||
                std::bit_cast<std::uint32_t>(prism::math::acos(unit[lane])) !=
                    std::bit_cast<std::uint32_t>(inverse[lane])) {
                return false;
            }
            maxSin = std::max(maxSin, std::abs(result.sin[lane] - std::sin(double(angles[lane]))));
            maxCos = std::max(maxCos, std::abs(result.cos[lane] - std::cos(double(angles[lane]))));
            maxAcos = std::max(maxAcos, std::abs(inverse[lane] - std::acos(double(unit[lane]))));
        }
    }
    std::cout << "Maximum errors: " << maxSin << ' ' << maxCos << ' ' << maxAcos << '\n';
    if (maxSin >= 2e-7 || maxCos >= 2e-7 || maxAcos >= 5e-7) {
        return false;
    }
    volatile float input = inputs[0];
    auto runtimeInputs = inputs;
    runtimeInputs[0] = input;
    const auto runtime = prism::math::sinCos(runtimeInputs);
    const auto runtimeAcos = prism::math::acos(V(std::array<float, 4>{-1, -0.5f, 0.5f, 1}));
    for (std::size_t i = 0; i < 4; ++i) {
        if (std::bit_cast<std::uint32_t>(runtime.sin[i]) !=
                std::bit_cast<std::uint32_t>(compileTime.sin[i]) ||
            std::bit_cast<std::uint32_t>(runtime.cos[i]) !=
                std::bit_cast<std::uint32_t>(compileTime.cos[i]) ||
            std::bit_cast<std::uint32_t>(runtimeAcos[i]) !=
                std::bit_cast<std::uint32_t>(compileAcos[i])) {
            return false;
        }
    }
    if (!std::isnan(prism::math::acos(1.01f)) ||
        !std::isnan(prism::math::sinCos(std::numeric_limits<float>::infinity()).sin) ||
        !std::signbit(prism::math::sinCos(-0.0f).sin)) {
        return false;
    }
    prism::test::AllocGuard guard;
    const auto guarded = prism::math::sinCos(runtimeInputs);
    return guarded.sin[0] == runtime.sin[0] && guard.counts().empty();
}
}

int main() {
    if (!check()) {
        std::cerr << "Trigonometry contract failed\n";
        return 1;
    }
    return 0;
}
