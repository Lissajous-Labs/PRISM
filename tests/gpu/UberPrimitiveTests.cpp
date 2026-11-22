#include "../support/ColorLab.h"
#include "../support/PixelOracle.h"
#include "../support/ReferenceScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "backends/metal/MetalBackend.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <vector>

namespace {
using namespace prism;
constexpr int skipped = 77;

std::vector<test::Pixel> toPixels(const std::vector<float> &rgba) {
    std::vector<test::Pixel> pixels(rgba.size() / 4);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = {rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2], rgba[i * 4 + 3]};
    }
    return pixels;
}

bool checkLayout(const backends::MetalDevice &device) {
    std::array<std::uint32_t, 16> values{};
    if (!device.reflectLayout(values)) {
        return false;
    }
    return std::bit_cast<float>(values[0]) == 1.5f && std::bit_cast<float>(values[1]) == -2.25f &&
           values[2] == 0x12345 && values[3] == 0xA1B2C3D4 && values[4] == 0x0F1E2D3C &&
           values[5] == 77 && values[6] == 513 && values[7] == 1027 && values[8] == 99 &&
           std::bit_cast<float>(values[9]) == 3.75f && std::bit_cast<float>(values[10]) == -0.5f &&
           values[11] == 6 && values[12] == 42 && values[13] == sizeof(render::PrimitiveDesc) &&
           values[14] == sizeof(render::TransformRecord) &&
           values[15] == sizeof(render::ClipRecord);
}

bool checkParity(backends::MetalDevice &device) {
    const test::ReferenceTextures textures;
    render::DrawList list;
    test::buildReferenceScene(list, 0);
    backends::CpuRefSurface reference({{128, 128}});
    auto gpu = device.createRenderSurface({{128, 128}});
    const auto expected = test::renderFull(reference, test::viewOf(list, textures.views));
    const auto actual = test::renderFull(*gpu, test::viewOf(list, textures.views));
    if (expected.empty() || actual.empty()) {
        return false;
    }
    const auto difference = test::comparePixels(toPixels(actual), toPixels(expected));
    double worstDeltaE = 0;
    for (std::size_t i = 0; i < expected.size(); i += 4) {
        const auto a = test::labFromLinear(actual[i], actual[i + 1], actual[i + 2]);
        const auto b = test::labFromLinear(expected[i], expected[i + 1], expected[i + 2]);
        worstDeltaE = std::max(worstDeltaE, test::deltaE2000(a, b));
    }
    std::cout << "GPU parity: PSNR " << difference.psnr << " dB, max " << difference.maximumAbsolute
              << ", worst DeltaE2000 " << worstDeltaE << '\n';
    return difference.psnr >= 52 && worstDeltaE <= 0.5;
}

bool checkPartialDamage(backends::MetalDevice &device) {
    const test::ReferenceTextures textures;
    render::DrawList list;
    auto incremental = device.createRenderSurface({{128, 128}});
    auto reference = device.createRenderSurface({{128, 128}});
    test::buildReferenceScene(list, 0);
    static_cast<void>(test::renderFull(*incremental, test::viewOf(list, textures.views)));
    test::buildReferenceScene(list, 9);
    hal::DamageRegion damage({0, 0, 90, 70});
    damage.add({60, 70, 120, 120});
    std::vector<float> partial(128 * 128 * 4);
    if (!incremental->render({test::viewOf(list, textures.views), &damage, {0, 0, 0, 1}}) ||
        !incremental->readback(partial)) {
        return false;
    }
    return partial == test::renderFull(*reference, test::viewOf(list, textures.views));
}

bool checkEarlyDepth(backends::MetalDevice &device) {
    render::DrawList list;
    for (int layer = 0; layer < 8; ++layer) {
        const float inset = static_cast<float>(layer) * 2;
        list.roundedRect({inset, inset, 256 - inset, 256 - inset}, {4, 4, 4, 4},
                         render::packColor(static_cast<std::uint8_t>(30 * layer), 90, 160, 255));
    }
    list.roundedRect({60, 60, 200, 200}, {10, 10, 10, 10}, render::packColor(255, 255, 255, 90));
    auto surface = device.createRenderSurface({{256, 256}});
    auto &metal = static_cast<backends::MetalSurface &>(*surface);
    metal.setFragmentCounting(true);
    metal.setEarlyDepth(false);
    const auto painter = test::renderFull(metal, test::viewOf(list));
    const auto withoutDepth = metal.lastFragmentCount();
    metal.setEarlyDepth(true);
    const auto early = test::renderFull(metal, test::viewOf(list));
    const auto withDepth = metal.lastFragmentCount();
    const auto difference = test::comparePixels(toPixels(early), toPixels(painter));
    const double ratio = static_cast<double>(withoutDepth) / std::max<std::uint32_t>(withDepth, 1);
    std::cout << "Fragments without early depth " << withoutDepth << ", with " << withDepth
              << ", ratio " << ratio << ", max difference " << difference.maximumAbsolute << '\n';
    return ratio >= 2 && difference.maximumAbsolute < 2e-3;
}

bool checkIsolation(backends::MetalDevice &device) {
    render::DrawList list;
    list.roundedRect({2, 2, 30, 30}, {4, 4, 4, 4}, test::white);
    auto first = device.createRenderSurface({{32, 32}});
    auto second = device.createRenderSurface({{32, 32}});
    hal::DamageRegion damage({0, 0, 32, 32});
    first->loseDevice();
    const bool failed = !first->render({test::viewOf(list), &damage, {0, 0, 0, 1}});
    const bool siblingOk = second->render({test::viewOf(list), &damage, {0, 0, 0, 1}});
    const bool recovered =
        first->rebuild() && first->render({test::viewOf(list), &damage, {0, 0, 0, 1}});
    return failed && siblingOk && recovered;
}

bool checkArcCaps(backends::MetalDevice &device) {
    render::DrawList list;
    const auto transform = *list.addTransform(math::Affine2D::scale(1.3, 0.7));
    list.setTransform(transform);
    const std::array caps{math::ArcCap::round, math::ArcCap::butt, math::ArcCap::square};
    for (std::size_t row = 0; row < caps.size(); ++row) {
        for (int column = 0; column < 6; ++column) {
            list.arc({20.25 + 32 * column, 21.75 + 32 * row}, 8.25f + column * 0.125f,
                     0.75f + row * 0.5f, column * 0.7f - 1.3f, 0.15f + column * 0.55f, caps[row],
                     render::packColor(245, 247, 217, 220));
        }
    }
    backends::CpuRefSurface reference({{320, 128}});
    auto gpu = device.createRenderSurface({{320, 128}});
    const auto expected = test::renderFull(reference, test::viewOf(list));
    const auto actual = test::renderFull(*gpu, test::viewOf(list));
    if (expected.empty() or actual.empty()) {
        return false;
    }
    const auto difference = test::comparePixels(toPixels(actual), toPixels(expected));
    std::cout << "Analytic arc cap GPU maximum error " << difference.maximumAbsolute << '\n';
    return difference.maximumAbsolute < 2e-3;
}

bool checkShadowScaleChanges(backends::MetalDevice &device) {
    backends::CpuRefSurface reference({{256, 192}});
    auto gpu = device.createRenderSurface({{256, 192}});
    render::DrawList list;
    const std::array<double, 8> scales{
        2, 0.5, 4, 0.75, std::nextafter(2.0f, 0.0f), std::nextafter(2.0f, 3.0f), 1, 2};
    for (const double scale : scales) {
        list.clear();
        const auto first = *list.addTransform(math::Affine2D::scale(scale, 0.75 * scale));
        const auto second = *list.addTransform(math::Affine2D::scale(0.5 * scale, scale));
        for (int i = 0; i < 4; ++i) {
            list.setTransform(i % 2 == 0 ? first : second);
            const float x = 5.25f + 18 * i, y = 10.75f + 8 * i;
            list.boxShadow({x, y, x + 20, y + 20}, 3, 4, render::packColor(245, 247, 217, 200));
        }
        const math::PremultipliedColor clear{0.1, 0.2, 0.3, 0.6};
        const auto expected = test::renderFull(reference, test::viewOf(list), clear);
        const auto actual = test::renderFull(*gpu, test::viewOf(list), clear);
        if (expected.empty() or actual.empty()) {
            return false;
        }
        const auto difference = test::comparePixels(toPixels(actual), toPixels(expected));
        if (difference.psnr < 52) {
            std::cerr << "Shadow transform cache mismatch at scale " << scale << ": PSNR "
                      << difference.psnr << '\n';
            return false;
        }
    }
    return true;
}
}

int main() {
    auto created = backends::MetalDevice::create();
    if (!created) {
        std::cout << "No native GPU device available\n";
        return skipped;
    }
    auto &device = static_cast<backends::MetalDevice &>(*created);
    if (!checkLayout(device) || !checkParity(device) || !checkPartialDamage(device) ||
        !checkEarlyDepth(device) || !checkIsolation(device) || !checkArcCaps(device) ||
        !checkShadowScaleChanges(device)) {
        std::cerr << "Native GPU contract failed\n";
        return 1;
    }
    return 0;
}
