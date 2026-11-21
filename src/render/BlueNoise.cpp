#include "render/BlueNoise.h"

#include "math/Hash64.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace prism::render {
BlueNoise::BlueNoise() {
    std::array<bool, 4096> occupied{};
    std::array<float, 4096> density{};
    std::array<float, 121> kernel{};
    for (int y = -5; y <= 5; ++y) {
        for (int x = -5; x <= 5; ++x) {
            kernel[static_cast<std::size_t>((y + 5) * 11 + x + 5)] =
                static_cast<float>(std::exp(-(x * x + y * y) / (2 * 1.5 * 1.5)));
        }
    }
    const auto change = [&](std::size_t index, bool value) {
        const int cx = static_cast<int>(index & 63), cy = static_cast<int>(index >> 6);
        const float delta = value ? 1.0f : -1.0f;
        occupied[index] = value;
        for (int y = -5; y <= 5; ++y) {
            for (int x = -5; x <= 5; ++x) {
                const auto target =
                    static_cast<std::size_t>(((cy + y + 64) & 63) * 64 + ((cx + x + 64) & 63));
                density[target] += delta * kernel[static_cast<std::size_t>((y + 5) * 11 + x + 5)];
            }
        }
    };
    const auto extremum = [&](bool value) {
        std::size_t best = 0;
        float score = value ? -1e30f : 1e30f;
        for (std::size_t i = 0; i < occupied.size(); ++i) {
            if (occupied[i] == value && (value ? density[i] > score : density[i] < score)) {
                best = i;
                score = density[i];
            }
        }
        return best;
    };
    std::size_t count = 0;
    for (std::size_t i = 0; i < occupied.size(); ++i) {
        if ((math::mix64(i + 0xC4A7935Du) & 15) == 0) {
            change(i, true);
            ++count;
        }
    }
    for (int i = 0; i < 10000; ++i) {
        const auto cluster = extremum(true);
        change(cluster, false);
        const auto hole = extremum(false);
        change(hole, true);
        if (cluster == hole) {
            break;
        }
    }
    const auto seed = occupied;
    const auto seedDensity = density;
    for (std::size_t rank = count; rank > 0;) {
        const auto index = extremum(true);
        values_[index] = static_cast<float>((--rank) >> 4) / 255;
        change(index, false);
    }
    occupied = seed;
    density = seedDensity;
    for (std::size_t rank = count; rank < occupied.size(); ++rank) {
        const auto index = extremum(false);
        values_[index] = static_cast<float>(rank >> 4) / 255;
        change(index, true);
    }
}
}
