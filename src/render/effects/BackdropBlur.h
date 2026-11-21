#pragma once

#include "hal/DamageRegion.h"
#include "render/effects/EffectDraw.h"
#include "render/effects/FloatImage.h"

namespace prism::render {
enum class BlurMethod : std::uint8_t { paired, separable, kawase };

[[nodiscard]] BlurMethod blurMethodFor(double sigma) noexcept;
[[nodiscard]] std::int32_t effectMargin(double sigma) noexcept;
[[nodiscard]] hal::Rect effectCrop(const EffectDraw &effect, std::uint32_t width,
                                   std::uint32_t height) noexcept;
[[nodiscard]] double effectMask(const EffectDraw &effect, double x, double y) noexcept;
void blurCrop(const FloatImage &crop, FloatImage &output, double sigma);
}
