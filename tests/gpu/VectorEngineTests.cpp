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
    std::cout << "GPU vector coverage worst difference: " << worst << '\n';
    if (worst >= 1e-3) {
        std::cerr << "GPU vector engine contract failed\n";
        return 1;
    }
    return 0;
}
