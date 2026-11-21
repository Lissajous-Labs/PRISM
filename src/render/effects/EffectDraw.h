#pragma once

#include <cstdint>

namespace prism::render {
struct EffectDraw {
    float rect[4];
    float radii[4];
    float sigma;
    std::uint32_t insertAfter;
};

static_assert(sizeof(EffectDraw) == 40);
}
