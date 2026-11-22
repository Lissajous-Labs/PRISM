#include "../support/Pcg.h"
#include "../support/ReferenceScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "backends/metal/MetalBackend.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
using namespace prism;

void randomPath(test::Pcg &random, render::DrawList &list) {
    render::VectorPath path;
    const auto point = [&] { return math::Vector2{random.range(-8, 136), random.range(-8, 136)}; };
    path.moveTo(point());
    const int pieces = 2 + static_cast<int>(random.next() % 6);
    for (int i = 0; i < pieces; ++i) {
        switch (random.next() % 3) {
        case 0:
            path.lineTo(point());
            break;
        case 1:
            path.quadTo(point(), point());
            break;
        default:
            path.cubicTo(point(), point(), point());
            break;
        }
    }
    path.close();
    const auto rule = random.unit() < 0.5 ? render::FillRule::nonZero : render::FillRule::evenOdd;
    list.fillPath(path, render::packColor(255, 255, 255, 255), rule);
}

void clippedScene(render::DrawList &list, int frame) {
    list.clear();
    list.roundedRect({0, 0, 513, 257}, {}, render::packColor(19, 19, 19, 255));
    const auto clip = list.addClip(render::ClipKind::roundedRect, {17.25f, 12.5f, 410.75f, 230.25f},
                                   {11, 11, 11, 11});
    list.setClip(clip);
    const double x = 20.25 + frame * 2;
    render::VectorPath path;
    path.moveTo({x, -5.5});
    path.lineTo({470.5, 190.75});
    path.lineTo({x, 251.25});
    path.close();
    list.fillPath(path, render::packColor(245, 247, 217, 220));
    list.setClip(render::noClip);
    list.roundedRect({40, 35, 180, 140}, {6, 6, 6, 6}, render::packColor(255, 169, 35, 255));
    list.fillPath(path, render::packColor(88, 205, 251, 80));
}
}

int main() {
    auto created = backends::MetalDevice::create();
    if (!created) {
        std::cout << "No native GPU device available\n";
        return 77;
    }
    auto &device = static_cast<backends::MetalDevice &>(*created);
    test::Pcg random(97);
    backends::CpuRefSurface reference({{128, 128}});
    auto gpu = device.createRenderSurface({{128, 128}});
    double worst = 0;
    render::DrawList list;
    for (int trial = 0; trial < 100; ++trial) {
        list.clear();
        randomPath(random, list);
        if (trial % 3 == 0) {
            randomPath(random, list);
        }
        const auto expected = test::renderFull(reference, test::viewOf(list));
        const auto actual = test::renderFull(*gpu, test::viewOf(list));
        if (expected.empty() || actual.empty()) {
            return 1;
        }
        for (std::size_t i = 0; i < expected.size(); ++i) {
            worst = std::max(worst, static_cast<double>(std::abs(expected[i] - actual[i])));
        }
    }
    reference.resize({513, 257});
    gpu->resize({513, 257});
    list.clear();
    render::VectorPath wide;
    wide.moveTo({-3.25, 2.75});
    wide.lineTo({511.5, 19.125});
    wide.lineTo({490.25, 259.5});
    wide.lineTo({11.75, 242.125});
    wide.close();
    wide.moveTo({100.5, 70.25});
    wide.lineTo({400.75, 80.125});
    wide.lineTo({350.25, 201.5});
    wide.close();
    list.fillPath(wide, render::packColor(255, 255, 255, 255), render::FillRule::evenOdd);
    const auto expected = test::renderFull(reference, test::viewOf(list));
    const auto actual = test::renderFull(*gpu, test::viewOf(list));
    if (expected.empty() or actual.empty()) {
        return 1;
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        worst = std::max(worst, static_cast<double>(std::abs(expected[i] - actual[i])));
    }

    list.clear();
    list.fillPath(wide, render::packColor(255, 255, 255, 255), render::FillRule::evenOdd);
    for (unsigned i = 0; i < 64; ++i) {
        render::VectorPath path;
        const double x = static_cast<double>(i % 8) * 64 - 12.25;
        const double y = static_cast<double>(i / 8) * 32 - 9.75;
        path.moveTo({x, y});
        path.lineTo({x + 1.5 + (i % 8) * 5.75, y + 2.25});
        path.lineTo({x + 10.125, y + 7.75 + (i % 5) * 4.25});
        path.close();
        list.fillPath(path, render::packColor(80, 160, 240, 127),
                      i % 2 ? render::FillRule::evenOdd : render::FillRule::nonZero);
        if (i % 5 == 0) {
            list.roundedRect({50, 40, 70, 55}, {}, render::packColor(220, 40, 90, 255));
        }
    }
    const auto batchExpected = test::renderFull(reference, test::viewOf(list));
    const auto batchActual = test::renderFull(*gpu, test::viewOf(list));
    if (batchExpected.empty() or batchActual.empty()) {
        return 1;
    }
    for (std::size_t i = 0; i < batchExpected.size(); ++i) {
        worst = std::max(worst, static_cast<double>(std::abs(batchExpected[i] - batchActual[i])));
    }

    list.clear();
    for (unsigned i = 0; i < 64; ++i) {
        render::VectorPath path;
        const double x = static_cast<double>((i * 43) % 520) - 12.25;
        const double y = static_cast<double>((i * 29) % 270) - 9.75;
        path.moveTo({x, y});
        path.lineTo({x + 1.5 + i * 3.75, y + 2.25});
        path.lineTo({x + 10.125, y + 7.75 + i * 2.25});
        path.close();
        list.fillPath(path, render::packColor(static_cast<std::uint8_t>(i * 3), 160, 240, 127));
        if (i % 5 == 0) {
            list.roundedRect({50, 40, 70, 55}, {}, render::packColor(220, 40, 90, 128));
        }
    }
    const auto layeredExpected = test::renderFull(reference, test::viewOf(list));
    const auto layeredActual = test::renderFull(*gpu, test::viewOf(list));
    if (layeredExpected.empty() or layeredActual.empty()) {
        return 1;
    }
    double squaredError = 0, squaredReference = 0;
    for (std::size_t i = 0; i < layeredExpected.size(); ++i) {
        if (i % 4 == 3) {
            continue;
        }
        const double error = static_cast<double>(layeredExpected[i]) - layeredActual[i];
        squaredError += error * error;
        squaredReference += static_cast<double>(layeredExpected[i]) * layeredExpected[i];
    }
    const double relativeRms = std::sqrt(squaredError / squaredReference);
    std::cout << "Layered half-float vector relative RGB RMS: " << relativeRms << '\n';
    if (not std::isfinite(relativeRms) or relativeRms >= 0.0065) {
        return 1;
    }

    std::vector<float> partialExpected(static_cast<std::size_t>(513 * 257 * 4));
    std::vector<float> partialActual(partialExpected.size());
    hal::DamageRegion damage({0, 0, 513, 257});
    for (int frame = 0; frame < 10; ++frame) {
        clippedScene(list, frame);
        const auto view = test::viewOf(list);
        if (not reference.render({view, &damage, {0, 0, 0, 1}}) or
            not gpu->render({view, &damage, {0, 0, 0, 1}}) or
            not reference.readback(partialExpected) or not gpu->readback(partialActual)) {
            return 1;
        }
        for (std::size_t i = 0; i < partialExpected.size(); ++i) {
            worst = std::max(worst,
                             static_cast<double>(std::abs(partialExpected[i] - partialActual[i])));
        }
        damage.assign({9, 7, 98, 179});
        damage.add({270, 48, 497, 251});
    }

    constexpr std::array extents{hal::Extent{16, 1601}, hal::Extent{1601, 16},
                                 hal::Extent{257, 769}};
    for (const auto extent : extents) {
        reference.resize(extent);
        gpu->resize(extent);
        list.clear();
        render::VectorPath path;
        const double width = extent.width, height = extent.height;
        path.moveTo({-7.25, -3.75});
        path.lineTo({width + 5.125, height * 0.7});
        path.lineTo({width * 0.25, height + 3.25});
        path.close();
        list.fillPath(path, render::packColor(255, 255, 255, 255));
        const auto expectedShape = test::renderFull(reference, test::viewOf(list));
        const auto actualShape = test::renderFull(*gpu, test::viewOf(list));
        if (expectedShape.empty() or actualShape.empty()) {
            return 1;
        }
        for (std::size_t i = 0; i < expectedShape.size(); ++i) {
            worst =
                std::max(worst, static_cast<double>(std::abs(expectedShape[i] - actualShape[i])));
        }
    }

    std::cout << "GPU vector coverage worst difference: " << worst << '\n';
    if (worst >= 1e-3) {
        std::cerr << "GPU vector engine contract failed\n";
        return 1;
    }
    return 0;
}
