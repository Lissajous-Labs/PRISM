#include "math/SimdVec.h"
#include "../support/AllocGuard.h"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {
using prism::math::SimdVec;
using F = SimdVec<float, 4>;
constexpr F first(std::array<float, 4>{1.25f, -2.5f, 3.0f, 8.0f});
constexpr F second(std::array<float, 4>{2.0f, 0.5f, -4.0f, 0.25f});
constexpr auto expected = (first * second + first) / second;
static_assert(first.shuffle<3, 2, 1, 0>()[0] == 8);
static_assert(first.sum() == 9.75f);
static_assert(SimdVec<std::uint32_t, 4>(0).isZero());
static_assert(prism::math::bmask(SimdVec<std::uint8_t, 4>(std::array<std::uint8_t, 4>{0, 128, 255, 1})) == 6);

bool check() {
    volatile float input = 1.25f;
    F actualFirst = first;
    actualFirst[0] = input;
    const F result = (actualFirst * second + actualFirst) / second;
    for (std::size_t i = 0; i < 4; ++i) {
        if (std::bit_cast<std::uint32_t>(result[i]) != std::bit_cast<std::uint32_t>(expected[i])) {
            return false;
        }
    }
    if (actualFirst.shuffle<2, 0, 3, 1>()[1] != input || actualFirst.sum() != first.sum()) {
        return false;
    }
    const auto negZero = F(-0.0f);
    if (!negZero.isZero() || !F(0).isZero() || F(std::numeric_limits<float>::quiet_NaN()).isZero()) {
        return false;
    }
    const auto bits = negZero.bitsAs<std::uint32_t>();
    const auto positive = (bits ^ SimdVec<std::uint32_t, 4>(0x80000000U)).bitsAs<float>();
    if (!positive.isZero() || std::bit_cast<std::uint32_t>(positive[0]) != 0) {
        return false;
    }
    alignas(32) std::array<float, 4> stored{};
    prism::test::AllocGuard guard;
    actualFirst.streamStore(stored.data());
    F::finishStreamStores();
    return stored == first.lanes() && guard.counts().empty();
}
}

int main() {
    if (!check()) {
        std::cerr << "Vector algebra contract failed\n";
        return 1;
    }
    return 0;
}
