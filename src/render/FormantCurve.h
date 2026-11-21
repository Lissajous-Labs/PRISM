#pragma once

#include "math/Color.h"
#include "render/FrameView.h"
#include "render/effects/FloatImage.h"

#include <cstdint>

namespace prism::render {
struct alignas(16) FormantPayload {
    float rect[4];
    float domain[4];
    float style[4];
    std::uint32_t sections;
    std::uint32_t parallel;
    float fillDecay;
    float zeroLineAlpha;
    float coefficients[16][5];
};
static_assert(sizeof(FormantPayload) == 384);
[[nodiscard]] double formantHeight(const FormantPayload &payload, double dbRange,
                                   double x) noexcept;
[[nodiscard]] math::PremultipliedColor
shadeFormantCurve(const FrameView &frame, const PrimitiveDesc &desc, math::Vector2 local) noexcept;
void rasterFormantCurve(const FrameView &frame, const PrimitiveDesc &desc, FloatImage &image,
                        std::uint32_t width, std::uint32_t height);
}
