#pragma once

#include "hal/HalTypes.h"
#include "math/Color.h"

namespace prism::render {
struct ShadeResult {
    math::PremultipliedColor color;
    double coverage[3];
    bool subpixel;
};

[[nodiscard]] inline math::PremultipliedColor blend(hal::BlendMode mode, const ShadeResult &source,
                                                    math::PremultipliedColor destination) noexcept {
    if (source.subpixel) {
        const auto channel = [&](double s, double cover, double d) { return s + (1 - cover) * d; };
        return {channel(source.color.r, source.coverage[0], destination.r),
                channel(source.color.g, source.coverage[1], destination.g),
                channel(source.color.b, source.coverage[2], destination.b),
                source.color.a + (1 - source.color.a) * destination.a};
    }
    switch (mode) {
    case hal::BlendMode::sourceOver:
        return math::sourceOver(source.color, destination);
    case hal::BlendMode::screen:
        return math::screen(source.color, destination);
    case hal::BlendMode::multiply:
        return math::multiply(source.color, destination);
    case hal::BlendMode::additive:
        return {source.color.r + destination.r, source.color.g + destination.g,
                source.color.b + destination.b, source.color.a + destination.a};
    case hal::BlendMode::replace:
        return source.color;
    }
    return destination;
}
}
