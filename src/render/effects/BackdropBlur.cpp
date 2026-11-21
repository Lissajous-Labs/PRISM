#include "render/effects/BackdropBlur.h"

#include "math/Coverage.h"
#include "math/SdfShapes.h"
#include "render/effects/DualKawase.h"
#include "render/effects/GaussianBlur.h"

#include <algorithm>
#include <cmath>

namespace prism::render {
BlurMethod blurMethodFor(double sigma) noexcept {
    if (sigma <= 4) {
        return BlurMethod::paired;
    }
    return sigma <= 16 ? BlurMethod::separable : BlurMethod::kawase;
}

std::int32_t effectMargin(double sigma) noexcept {
    return static_cast<std::int32_t>(std::ceil(3 * sigma)) + 4;
}

hal::Rect effectCrop(const EffectDraw &effect, std::uint32_t width, std::uint32_t height) noexcept {
    const std::int32_t margin = effectMargin(effect.sigma);
    return {std::clamp(static_cast<std::int32_t>(std::floor(effect.rect[0])) - margin, 0,
                       static_cast<std::int32_t>(width)),
            std::clamp(static_cast<std::int32_t>(std::floor(effect.rect[1])) - margin, 0,
                       static_cast<std::int32_t>(height)),
            std::clamp(static_cast<std::int32_t>(std::ceil(effect.rect[2])) + margin, 0,
                       static_cast<std::int32_t>(width)),
            std::clamp(static_cast<std::int32_t>(std::ceil(effect.rect[3])) + margin, 0,
                       static_cast<std::int32_t>(height))};
}

double effectMask(const EffectDraw &effect, double x, double y) noexcept {
    const math::Vector2 center{(effect.rect[0] + effect.rect[2]) / 2.0,
                               (effect.rect[1] + effect.rect[3]) / 2.0};
    const math::Vector2 half{(effect.rect[2] - effect.rect[0]) / 2.0,
                             (effect.rect[3] - effect.rect[1]) / 2.0};
    const math::CornerRadii radii{effect.radii[0], effect.radii[1], effect.radii[2],
                                  effect.radii[3]};
    const auto field = [&](math::Vector2 p) { return math::roundedRect(p - center, half, radii); };
    constexpr double step = 1.0 / 64;
    const math::Vector2 p{x, y};
    const math::Vector2 gradient{
        (field(p + math::Vector2{step, 0}) - field(p - math::Vector2{step, 0})) / (2 * step),
        (field(p + math::Vector2{0, step}) - field(p - math::Vector2{0, step})) / (2 * step)};
    const double norm = math::length(gradient);
    return norm > 1e-12 ? math::boxCoverage(field(p) / norm, gradient) : (field(p) <= 0 ? 1 : 0);
}

void blurCrop(const FloatImage &crop, FloatImage &output, double sigma) {
    switch (blurMethodFor(sigma)) {
    case BlurMethod::paired:
        blurPaired(crop, output, sigma);
        break;
    case BlurMethod::separable:
        blurSeparable(crop, output, sigma);
        break;
    case BlurMethod::kawase:
        kawaseBlur(crop, output, sigma);
        break;
    }
}
}
