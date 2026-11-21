#include "../support/AllocGuard.h"
#include "../support/Pcg.h"
#include "backends/cpu/CpuRefBackend.h"
#include "render/DrawList.h"

#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

namespace {
using namespace prism;
constexpr std::uint32_t white = render::packColor(255, 255, 255, 255);

render::FrameView viewOf(const render::DrawList &list,
                         std::span<const render::TextureView> textures = {},
                         std::span<const float> gradients = {}) {
    return {list.primitives(), list.payload(), list.transforms(),
            list.clips(),      gradients,      textures};
}

std::vector<float> renderFull(render::RenderSurface &surface, const render::FrameView &view) {
    const auto extent = surface.extent();
    hal::DamageRegion damage(
        {0, 0, static_cast<std::int32_t>(extent.width), static_cast<std::int32_t>(extent.height)});
    std::vector<float> pixels(static_cast<std::size_t>(extent.width) * extent.height * 4);
    if (!surface.render({view, &damage, {0, 0, 0, 1}}) || !surface.readback(pixels)) {
        return {};
    }
    return pixels;
}

bool checkExactCoverage() {
    backends::CpuRefSurface surface({{32, 16}});
    render::DrawList list;
    list.roundedRect({10.25f, 2, 20.75f, 14}, {}, white);
    const auto pixels = renderFull(surface, viewOf(list));
    const auto red = [&](int x, int y) {
        return pixels[(static_cast<std::size_t>(y) * 32 + x) * 4];
    };
    return std::abs(red(10, 8) - 0.75f) < 1e-6f && std::abs(red(20, 8) - 0.75f) < 1e-6f &&
           red(15, 8) == 1.0f && red(9, 8) == 0.0f && red(21, 8) == 0.0f;
}

bool checkRotatedArea() {
    backends::CpuRefSurface surface({{96, 96}});
    render::DrawList list;
    const auto rotation =
        math::Affine2D::translation(48, 48) * math::Affine2D::rotation(std::numbers::pi / 5);
    list.setTransform(*list.addTransform(rotation));
    list.roundedRect({-30, -12, 30, 12}, {6, 6, 6, 6}, white);
    const auto pixels = renderFull(surface, viewOf(list));
    double coverage = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        coverage += pixels[i];
    }
    const double expected = 60 * 24 - (4 - std::numbers::pi) * 36;
    std::cout << "Rotated area: " << coverage << " expected " << expected << '\n';
    return std::abs(coverage - expected) / expected < 2e-3;
}

void buildScene(render::DrawList &list, float shift, const render::TextureView &) {
    list.clear();
    list.roundedRect({4, 4, 120, 60}, {8, 2, 12, 0}, render::packColor(40, 60, 90, 255),
                     render::BorderStyle{{2, 3, 4, 1}, render::packColor(200, 120, 40, 255)});
    list.boxShadow({20 + shift, 20, 60 + shift, 50}, 6, 4, render::packColor(0, 0, 0, 160));
    list.squircle({70, 10, 110, 50}, 5, render::packColor(250, 250, 250, 200));
    list.arc({40, 90}, 14, 4, 0.5f, 2.0f, math::ArcCap::round, render::packColor(255, 0, 0, 255));
    list.capsule({70 + shift, 80}, {110, 110}, 3, render::packColor(0, 255, 0, 255));
    list.quadBezier({5, 120}, {40, 70}, {80, 125}, 1.5f, render::packColor(0, 0, 255, 255));
    list.triangle({90, 70}, {125, 75}, {100, 120}, 3, render::packColor(255, 255, 0, 180));
    const auto clip =
        list.addClip(render::ClipKind::roundedRect, {0, 60, 64, 128}, {10, 10, 10, 10});
    list.setClip(clip);
    list.setBlend(hal::BlendMode::screen);
    list.roundedRect({10, 64, 70, 100}, {}, render::packColor(80, 80, 200, 200));
    list.setClip(render::noClip);
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

bool checkPartialDamage() {
    std::vector<float> texels(8 * 8 * 4);
    for (std::size_t i = 0; i < texels.size(); ++i) {
        texels[i] = static_cast<float>((i * 37) % 101) / 100.0f;
    }
    std::vector<float> strip{0.1f, 0.4f, 0.0f, 0.0f, 0.3f, 0.9f, 0.0f, 0.0f, 0.2f, 0.5f, 0.0f,
                             0.0f, 0.6f, 0.7f, 0.0f, 0.0f, 0.1f, 0.2f, 0.0f, 0.0f, 0.5f, 0.8f,
                             0.0f, 0.0f, 0.4f, 0.4f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    const std::array<render::TextureView, 2> textures{render::TextureView{8, 8, 4, texels},
                                                      render::TextureView{8, 1, 4, strip}};
    backends::CpuRefSurface incremental({{128, 128}});
    backends::CpuRefSurface reference({{128, 128}});
    render::DrawList list;
    buildScene(list, 0, textures[0]);
    if (renderFull(incremental, viewOf(list, textures)).empty()) {
        return false;
    }
    buildScene(list, 9, textures[0]);
    hal::DamageRegion damage({0, 0, 90, 70});
    damage.add({60, 70, 120, 120});
    std::vector<float> partial(128 * 128 * 4);
    if (!incremental.render({viewOf(list, textures), &damage, {0, 0, 0, 1}}) ||
        !incremental.readback(partial)) {
        return false;
    }
    const auto full = renderFull(reference, viewOf(list, textures));
    for (float value : full) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    {
        prism::test::AllocGuard guard;
        if (!incremental.render({viewOf(list, textures), &damage, {0, 0, 0, 1}}) ||
            !guard.counts().empty()) {
            return false;
        }
    }
    return partial == full;
}

bool checkLifecycle() {
    const auto registry = hal::DeviceRegistry::acquire(hal::BackendKind::cpuReference,
                                                       backends::CpuRefDevice::create);
    auto &device = static_cast<render::RenderDevice &>(registry->device());
    auto surface = device.createRenderSurface({{16, 16}});
    auto sibling = device.createRenderSurface({{16, 16}});
    render::DrawList list;
    list.roundedRect({2, 2, 14, 14}, {}, white);
    hal::DamageRegion damage({0, 0, 16, 16});
    surface->loseDevice();
    const bool failed = !surface->render({viewOf(list), &damage, {0, 0, 0, 1}});
    const bool siblingOk = sibling->render({viewOf(list), &damage, {0, 0, 0, 1}});
    const bool recovered =
        surface->rebuild() && surface->render({viewOf(list), &damage, {0, 0, 0, 1}});
    surface->resize({32, 8});
    return failed && siblingOk && recovered && surface->extent() == hal::Extent{32, 8};
}
}

int main() {
    if (!checkExactCoverage() || !checkRotatedArea() || !checkPartialDamage() ||
        !checkLifecycle()) {
        std::cerr << "Reference backend contract failed\n";
        return 1;
    }
    return 0;
}
