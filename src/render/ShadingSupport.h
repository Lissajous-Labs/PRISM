#pragma once

#include "math/ColorSpace.h"
#include "math/Coverage.h"
#include "math/Vector2.h"
#include "render/FrameView.h"

#include <cmath>
#include <cstdint>

namespace prism::render::detail {
[[nodiscard]] inline math::PremultipliedColor decodeColor(std::uint32_t packed) noexcept {
    const auto channel = [&](int shift) {
        return static_cast<double>((packed >> shift) & 0xFFu) / 255;
    };
    const double alpha = channel(24);
    return {math::srgbDecode(channel(0)) * alpha, math::srgbDecode(channel(8)) * alpha,
            math::srgbDecode(channel(16)) * alpha, alpha};
}

[[nodiscard]] inline math::PremultipliedColor scale(math::PremultipliedColor color,
                                                    double amount) noexcept {
    return math::withCoverage(color, amount);
}

template <class Field>
[[nodiscard]] double fieldCoverage(const TransformRecord &transform, math::Vector2 local,
                                   Field &&field) noexcept {
    constexpr double step = 1.0 / 64;
    const double distance = field(local);
    const double reach = 0.71 * std::sqrt(transform.inverse[0] * transform.inverse[0] +
                                          transform.inverse[1] * transform.inverse[1] +
                                          transform.inverse[2] * transform.inverse[2] +
                                          transform.inverse[3] * transform.inverse[3]);
    if (std::abs(distance) >= reach) {
        return distance < 0 ? 1 : 0;
    }
    const math::Vector2 localGradient{
        (field(local + math::Vector2{step, 0}) - field(local - math::Vector2{step, 0})) /
            (2 * step),
        (field(local + math::Vector2{0, step}) - field(local - math::Vector2{0, step})) /
            (2 * step)};
    const math::Vector2 physicalGradient{
        transform.inverse[0] * localGradient.x + transform.inverse[1] * localGradient.y,
        transform.inverse[2] * localGradient.x + transform.inverse[3] * localGradient.y};
    const double norm = math::length(physicalGradient);
    if (!(norm > 1e-12)) {
        return distance <= 0 ? 1 : 0;
    }
    return math::boxCoverage(distance / norm, physicalGradient);
}

[[nodiscard]] inline math::Vector2 toLocal(const TransformRecord &transform, double x,
                                           double y) noexcept {
    return {transform.inverse[0] * x + transform.inverse[2] * y + transform.inverse[4],
            transform.inverse[1] * x + transform.inverse[3] * y + transform.inverse[5]};
}
}
