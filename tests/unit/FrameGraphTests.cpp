#include "../support/AllocGuard.h"
#include "../support/Pcg.h"
#include "hal/FrameGraph.h"

#include <iostream>

namespace {
using namespace prism::hal;

bool overlaps(const TextureLifetime &a, const TextureLifetime &b) {
    const bool timeOverlap = a.firstPass <= b.lastPass && b.firstPass <= a.lastPass;
    const bool memoryOverlap = a.offset < b.offset + b.size && b.offset < a.offset + a.size;
    return timeOverlap && memoryOverlap;
}

bool validate(const FrameGraph &graph) {
    for (std::uint32_t i = 0; i < graph.textureCount(); ++i) {
        const auto &a = graph.lifetime(i);
        if (a.firstPass > a.lastPass) {
            continue;
        }
        if (a.offset + a.size > graph.heapSize() || a.offset % heapAlignment != 0) {
            return false;
        }
        for (std::uint32_t j = i + 1; j < graph.textureCount(); ++j) {
            if (overlaps(a, graph.lifetime(j))) {
                return false;
            }
        }
    }
    return true;
}

bool checkBlurChain() {
    FrameGraph graph;
    const PixelFormat hdr = PixelFormat::rgba16Float;
    const auto scene = *graph.createTexture({{2560, 1440}, hdr});
    const auto horizontal = *graph.createTexture({{2560, 1440}, hdr});
    const auto vertical = *graph.createTexture({{2560, 1440}, hdr});
    const auto down1 = *graph.createTexture({{1280, 720}, hdr});
    const auto down2 = *graph.createTexture({{640, 360}, hdr});
    const auto down3 = *graph.createTexture({{320, 180}, hdr});
    const auto up2 = *graph.createTexture({{640, 360}, hdr});
    const auto up1 = *graph.createTexture({{1280, 720}, hdr});
    const auto bloom = *graph.createTexture({{2560, 1440}, hdr});
    const auto output = *graph.createTexture({{2560, 1440}, hdr});
    const bool declared = graph.addPass({}, {scene}) && graph.addPass({scene}, {horizontal}) &&
                          graph.addPass({horizontal}, {vertical}) &&
                          graph.addPass({vertical}, {down1}) && graph.addPass({down1}, {down2}) &&
                          graph.addPass({down2}, {down3}) && graph.addPass({down3}, {up2}) &&
                          graph.addPass({up2}, {up1}) && graph.addPass({up1}, {bloom}) &&
                          graph.addPass({bloom, vertical}, {output});
    prism::test::AllocGuard guard;
    const bool compiled = graph.compile();
    const double ratio =
        static_cast<double>(graph.heapSize()) / static_cast<double>(graph.unaliasedSize());
    std::cout << "Aliased heap ratio: " << ratio << '\n';
    return declared && compiled && guard.counts().empty() && validate(graph);
}

bool checkSequentialBackdropBlurs() {
    FrameGraph graph;
    const PixelFormat hdr = PixelFormat::rgba16Float;
    const auto output = *graph.createTexture({{2560, 1440}, hdr});
    bool declared = static_cast<bool>(graph.addPass({}, {output}));
    for (int panel = 0; panel < 4; ++panel) {
        const auto source = *graph.createTexture({{1280, 720}, hdr});
        const auto horizontal = *graph.createTexture({{1280, 720}, hdr});
        const auto vertical = *graph.createTexture({{1280, 720}, hdr});
        declared = declared && graph.addPass({output}, {source}) &&
                   graph.addPass({source}, {horizontal}) && graph.addPass({horizontal}, {vertical}) &&
                   graph.addPass({vertical}, {output});
    }
    const bool compiled = graph.compile();
    const double ratio =
        static_cast<double>(graph.heapSize()) / static_cast<double>(graph.unaliasedSize());
    std::cout << "Sequential blur heap ratio: " << ratio << '\n';
    return declared && compiled && validate(graph) && ratio <= 0.5;
}

bool checkRandomGraphs(prism::test::Pcg &random) {
    for (int trial = 0; trial < 2000; ++trial) {
        FrameGraph graph;
        const auto textures = 2 + static_cast<std::uint32_t>(random.next() % 30);
        for (std::uint32_t i = 0; i < textures; ++i) {
            const auto side = 16 + static_cast<std::uint32_t>(random.next() % 512);
            static_cast<void>(graph.createTexture({{side, side / 2 + 1}, PixelFormat::rgba8Unorm}));
        }
        const auto passes = 1 + static_cast<std::uint32_t>(random.next() % 40);
        for (std::uint32_t pass = 0; pass < passes; ++pass) {
            const auto read = static_cast<std::uint32_t>(random.next() % textures);
            const auto write = static_cast<std::uint32_t>(random.next() % textures);
            if (!graph.addPass({read}, {write})) {
                return false;
            }
        }
        if (!graph.compile() || !validate(graph) || graph.heapSize() > graph.unaliasedSize()) {
            return false;
        }
    }
    FrameGraph graph;
    const auto texture = *graph.createTexture({{4, 4}, PixelFormat::r8Unorm});
    return !graph.addPass({texture + 1}, {}) && graph.addPass({}, {texture});
}
}

int main() {
    prism::test::Pcg random(61);
    if (!checkBlurChain() || !checkSequentialBackdropBlurs() || !checkRandomGraphs(random)) {
        std::cerr << "Frame graph contract failed\n";
        return 1;
    }
    return 0;
}
