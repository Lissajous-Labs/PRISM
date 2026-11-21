#include "../support/AllocGuard.h"
#include "../support/ReferenceScene.h"
#include "backends/cpu/CpuRefBackend.h"

#include <cmath>
#include <iostream>
#include <numbers>
#include <vector>

namespace {
using namespace prism;
using test::renderFull;
using test::viewOf;
using test::white;

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

bool checkPartialDamage() {
    const test::ReferenceTextures textures;
    backends::CpuRefSurface incremental({{128, 128}});
    backends::CpuRefSurface reference({{128, 128}});
    render::DrawList list;
    test::buildReferenceScene(list, 0);
    if (renderFull(incremental, viewOf(list, textures.views)).empty()) {
        return false;
    }
    test::buildReferenceScene(list, 9);
    hal::DamageRegion damage({0, 0, 90, 70});
    damage.add({60, 70, 120, 120});
    std::vector<float> partial(128 * 128 * 4);
    if (!incremental.render({viewOf(list, textures.views), &damage, {0, 0, 0, 1}}) ||
        !incremental.readback(partial)) {
        return false;
    }
    const auto full = renderFull(reference, viewOf(list, textures.views));
    for (float value : full) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    {
        prism::test::AllocGuard guard;
        if (!incremental.render({viewOf(list, textures.views), &damage, {0, 0, 0, 1}}) ||
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
