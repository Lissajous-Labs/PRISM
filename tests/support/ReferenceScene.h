#pragma once

#include "render/DrawList.h"
#include "render/RenderSurface.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace prism::test {
inline constexpr std::uint32_t white = render::packColor(255, 255, 255, 255);

struct ReferenceTextures {
    std::vector<float> texels;
    std::vector<float> strip;
    std::array<render::TextureView, 2> views;

    ReferenceTextures() : texels(8 * 8 * 4) {
        for (std::size_t i = 0; i < texels.size(); ++i) {
            texels[i] = static_cast<float>((i * 37) % 101) / 100.0f;
        }
        const float bands[8][2] = {{0.1f, 0.4f}, {0.3f, 0.9f}, {0.2f, 0.5f}, {0.6f, 0.7f},
                                   {0.1f, 0.2f}, {0.5f, 0.8f}, {0.4f, 0.4f}, {0.0f, 1.0f}};
        for (const auto &band : bands) {
            strip.insert(strip.end(), {band[0], band[1], 0.0f, 0.0f});
        }
        views = {render::TextureView{8, 8, 4, texels}, render::TextureView{8, 1, 4, strip}};
    }
    ReferenceTextures(const ReferenceTextures &) = delete;
    ReferenceTextures &operator=(const ReferenceTextures &) = delete;
};

inline render::FrameView viewOf(const render::DrawList &list,
                                std::span<const render::TextureView> textures = {},
                                std::span<const float> gradients = {}) {
    return {list.primitives(), list.payload(), list.transforms(), list.clips(),
            gradients,         textures,       list.paths(),      list.pathSegments()};
}

inline std::vector<float> renderFull(render::RenderSurface &surface, const render::FrameView &view,
                                     math::PremultipliedColor clear = {0, 0, 0, 1}) {
    const auto extent = surface.extent();
    hal::DamageRegion damage(
        {0, 0, static_cast<std::int32_t>(extent.width), static_cast<std::int32_t>(extent.height)});
    std::vector<float> pixels(static_cast<std::size_t>(extent.width) * extent.height * 4);
    if (!surface.render({view, &damage, clear}) || !surface.readback(pixels)) {
        return {};
    }
    return pixels;
}

inline void buildReferenceScene(render::DrawList &list, float shift) {
    list.clear();
    list.roundedRect({4, 4, 120, 60}, {8, 2, 12, 0}, render::packColor(40, 60, 90, 255),
                     render::BorderStyle{{2, 3, 4, 1}, render::packColor(200, 120, 40, 255)});
    list.boxShadow({20 + shift, 20, 60 + shift, 50}, 6, 4, render::packColor(0, 0, 0, 160));
    list.squircle({70, 10, 110, 50}, 5, render::packColor(250, 250, 250, 200));
    list.arc({40, 90}, 14, 4, 0.5f, 2.0f, math::ArcCap::round, render::packColor(255, 0, 0, 255));
    list.capsule({70 + shift, 80}, {110, 110}, 3, render::packColor(0, 255, 0, 255));
    list.quadBezier({5, 120}, {40, 70}, {80, 125}, 1.5f, render::packColor(0, 0, 255, 255));
    list.triangle({90, 70}, {125, 75}, {100, 120}, 3, render::packColor(255, 255, 0, 180));
    render::VectorPath star;
    for (int i = 0; i < 5; ++i) {
        const double angle = -1.5707963 + i * 2.513274;
        const math::Vector2 point{60 + 18 * std::cos(angle) + shift, 30 + 18 * std::sin(angle)};
        i == 0 ? star.moveTo(point) : star.lineTo(point);
    }
    star.close();
    list.fillPath(star, render::packColor(220, 40, 200, 230), render::FillRule::evenOdd);
    render::VectorPath curve;
    curve.moveTo({8, 40});
    curve.cubicTo({30, 0}, {50, 80}, {70 + shift, 20});
    render::StrokeStyle style;
    style.width = 3;
    style.cap = render::LineCap::round;
    style.join = render::LineJoin::round;
    list.strokePath(curve, style, render::packColor(255, 220, 0, 255));
    list.setClip(list.addClip(render::ClipKind::roundedRect, {0, 60, 64, 128}, {10, 10, 10, 10}));
    list.setBlend(hal::BlendMode::screen);
    list.roundedRect({10, 64, 70, 100}, {}, render::packColor(80, 80, 200, 200));
    list.setClip(render::noClip);
    list.setBlend(hal::BlendMode::multiply);
    list.roundedRect({80, 30, 126, 66}, {5, 5, 5, 5}, render::packColor(200, 180, 120, 230));
    list.setBlend(hal::BlendMode::sourceOver);
    list.texturedQuad(render::PrimitiveKind::glyphAlpha, {96, 96, 112, 112}, {0, 0, 1, 1}, 0,
                      white);
    list.texturedQuad(render::PrimitiveKind::glyphLcd, {112, 96, 128, 112}, {0, 0, 1, 1}, 0, white);
    list.texturedQuad(render::PrimitiveKind::glyphMtsdf, {96, 112, 112, 128}, {0, 0, 1, 1}, 0,
                      white, 4);
    list.texturedQuad(render::PrimitiveKind::texturedQuad, {112, 112, 128, 128}, {0, 0, 1, 1}, 0,
                      white);
    list.plotStrip({0, 100, 64, 128}, 1, 8, 1.5f, white);
}
}
