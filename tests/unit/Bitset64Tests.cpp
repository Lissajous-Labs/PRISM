#include "../support/AllocGuard.h"
#include "math/Bitset64.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {
constexpr bool compileTimeOrder() {
    prism::math::Bitset64<4097> bits;
    bits.set(4096);
    bits.set(0);
    bits.set(64);
    bits.reset(64);
    std::size_t expected[] = {0, 4096};
    std::size_t index = 0;
    bool ordered = true;
    bits.forEach(
        [&](std::size_t value) { ordered = ordered && index < 2 && expected[index++] == value; });
    return ordered && index == 2 && bits.any();
}
static_assert(compileTimeOrder());

template <std::size_t Capacity> bool matchesOracle(std::uint64_t seed) {
    auto bits = std::make_unique<prism::math::Bitset64<Capacity>>();
    std::vector<bool> oracle(Capacity);
    std::vector<std::size_t> visited;
    visited.reserve(Capacity);
    for (int step = 0; step < 20000; ++step) {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto index = static_cast<std::size_t>((seed >> 17) % Capacity);
        if ((seed >> 63) != 0) {
            bits->set(index);
            oracle[index] = true;
        } else {
            bits->reset(index);
            oracle[index] = false;
        }
        if (bits->test(index) != oracle[index]) {
            return false;
        }
    }
    bits->forEach([&](std::size_t index) { visited.push_back(index); });
    std::size_t cursor = 0;
    for (std::size_t index = 0; index < Capacity; ++index) {
        if (oracle[index] && (cursor == visited.size() || visited[cursor++] != index)) {
            return false;
        }
    }
    if (cursor != visited.size() || bits->any() != !visited.empty()) {
        return false;
    }
    {
        prism::test::AllocGuard guard;
        bits->clear();
        if (!guard.counts().empty()) {
            return false;
        }
    }
    bool empty = !bits->any();
    bits->forEach([&](std::size_t) { empty = false; });
    for (std::size_t index = 0; index < Capacity; ++index) {
        empty = empty && !bits->test(index);
    }
    return empty;
}
}

int main() {
    if (!matchesOracle<1>(1) || !matchesOracle<64>(2) || !matchesOracle<65>(3) ||
        !matchesOracle<4096>(4) || !matchesOracle<4097>(5) || !matchesOracle<262145>(6)) {
        std::cerr << "Bitset contract failed\n";
        return 1;
    }
    return 0;
}
