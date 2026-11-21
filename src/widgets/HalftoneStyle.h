#pragma once

#include "render/DrawList.h"

namespace prism::widgets {
struct HalftoneStyle {
    float columns = 52;
    float rows = 40;
    float gutterX = 0.20f;
    float gutterY = 0.38f;
    float radius = 0.45f;
    float minimumWidth = 0.52f;
    float widthPower = 0.75f;
    float screenMix = 0.85f;
    float thresholdSpread = 0.38f;
    float thresholdSoftness = 0.18f;
};
[[nodiscard]] inline render::HalftonePayload halftonePayload(render::RectF rect,
                                                             const HalftoneStyle &style,
                                                             std::uint32_t palette,
                                                             std::uint32_t noise) noexcept {
    const float px = (rect.x1 - rect.x0) / style.columns;
    const float py = (rect.y1 - rect.y0) / style.rows;
    render::HalftonePayload h{};
    h.rect[0] = rect.x0;
    h.rect[1] = rect.y0;
    h.rect[2] = rect.x1;
    h.rect[3] = rect.y1;
    h.basis[0] = px;
    h.basis[3] = py;
    h.invBasis[0] = 1 / px;
    h.invBasis[3] = 1 / py;
    h.origin[0] = rect.x0;
    h.origin[1] = rect.y0;
    h.gutter[0] = px * style.gutterX;
    h.gutter[1] = py * style.gutterY;
    h.spotParams[0] = py * (1 - style.gutterY) * style.radius;
    h.spotParams[1] = 4;
    h.spotParams[2] = style.minimumWidth;
    h.spotParams[3] = style.widthPower;
    h.screenParams[0] = style.screenMix;
    h.screenParams[1] = style.thresholdSpread;
    h.screenParams[2] = style.thresholdSoftness;
    h.screenParams[3] = 1.15f;
    h.cursorParams[1] = 0.35f;
    h.cursorParams[2] = 0.035f;
    h.cursorParams[3] = 1;
    h.paletteRow = palette;
    h.blueNoiseSlot = noise;
    return h;
}
}
