#include "../support/ReferenceScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "scene/SceneGraph.h"

#include <array>
#include <iostream>
#include <vector>

namespace {
using namespace prism;

constexpr hal::Extent extent{192, 160};

struct Fixture {
    scene::SceneGraph graph{extent};
    std::array<scene::NodeId, 8> groups{};
    std::array<std::array<scene::NodeId, 12>, 8> leaves{};

    Fixture() {
        for (std::uint32_t g = 0; g < groups.size(); ++g) {
            groups[g] = graph.create(scene::rootNode);
            auto transform = math::Affine2D::translation(4 + (g % 4) * 47.0, 6 + (g / 4) * 78.0);
            if (g == 3) {
                transform = transform * math::Affine2D::rotation(0.12);
            }
            graph.setTransform(groups[g], transform);
            graph.setClip(groups[g], render::RectF{0, 0, 42, 70},
                          g % 2 == 0 ? math::CornerRadii{6, 6, 6, 6} : math::CornerRadii{});
            for (std::uint32_t i = 0; i < leaves[g].size(); ++i) {
                leaves[g][i] = graph.create(groups[g]);
                const float x = static_cast<float>((i % 3) * 15) - 2;
                const float y = static_cast<float>((i / 3) * 18) - 3;
                graph.setPaint(leaves[g][i],
                               scene::Paint::box(
                                   {x, y, x + 17.5f, y + 14.25f}, {3, 3, 3, 3},
                                   render::packColor(static_cast<std::uint8_t>(30 * g),
                                                     static_cast<std::uint8_t>(20 * i), 200, 220)));
            }
        }
    }
};

std::vector<float> renderFresh(const scene::SceneGraph &graph) {
    render::DrawList list;
    graph.record(list);
    backends::CpuRefSurface surface({extent});
    return test::renderFull(surface, test::viewOf(list));
}

bool checkIncrementalRedraw() {
    Fixture fixture;
    hal::DamageRegion damage;
    const std::size_t initial = fixture.graph.update(damage);
    backends::CpuRefSurface surface({extent});
    render::DrawList list;
    fixture.graph.record(list);
    static_cast<void>(test::renderFull(surface, test::viewOf(list)));
    for (const std::uint32_t g : {0u, 4u, 6u}) {
        fixture.graph.setPaint(fixture.leaves[g][4],
                               scene::Paint::box({13, 15, 30, 29}, {7, 7, 7, 7},
                                                 render::packColor(250, 240, 10, 255)));
    }
    fixture.graph.setTransform(fixture.groups[2], math::Affine2D::translation(100, 12));
    fixture.graph.destroy(fixture.leaves[5][7]);
    const scene::NodeId added = fixture.graph.create(fixture.groups[1]);
    fixture.graph.setPaint(
        added, scene::Paint::box({20, 30, 40, 44}, {}, render::packColor(10, 250, 90, 200)));
    fixture.graph.setVisible(fixture.groups[7], false);
    const std::size_t visits = fixture.graph.update(damage);
    list.clear();
    fixture.graph.record(list, &damage);
    std::vector<float> incremental(extent.width * extent.height * 4);
    if (!surface.render({test::viewOf(list), &damage, {0, 0, 0, 1}}) ||
        !surface.readback(incremental)) {
        return false;
    }
    const bool exact = incremental == renderFresh(fixture.graph);
    const std::int64_t area = damage.area();
    const bool partial = area < static_cast<std::int64_t>(extent.width) * extent.height / 2;
    const std::size_t idle = fixture.graph.update(damage);
    std::cout << "Initial visits " << initial << ", mutated visits " << visits << ", damage area "
              << area << ", exact " << exact << '\n';
    return initial == 105 && visits == 30 && exact && partial && idle == 0 && damage.empty();
}

bool checkSparseTraversal() {
    scene::SceneGraph graph({4096, 4096});
    std::vector<scene::NodeId> nodes;
    for (int i = 0; i < 20000; ++i) {
        nodes.push_back(graph.create(scene::rootNode));
        const auto x = static_cast<float>((i % 200) * 20);
        const auto y = static_cast<float>((i / 200) * 20);
        graph.setPaint(nodes.back(), scene::Paint::box({x, y, x + 10, y + 10}, {}, 0xFFFFFFFF));
    }
    hal::DamageRegion damage;
    static_cast<void>(graph.update(damage));
    for (const int index : {7, 9001, 19999, 4242, 12345}) {
        graph.setPaint(nodes[static_cast<std::size_t>(index)],
                       scene::Paint::box({0, 0, 4, 4}, {}, 0xFF0000FF));
    }
    const std::size_t visits = graph.update(damage);
    graph.destroy(nodes[3]);
    const scene::NodeId reused = graph.create(scene::rootNode);
    const std::size_t afterReuse = graph.update(damage);
    return visits == 5 && reused == nodes[3] && afterReuse == 1 && graph.liveCount() == 20001;
}
}

int main() {
    if (!checkIncrementalRedraw() || !checkSparseTraversal()) {
        std::cerr << "Scene graph contract failed\n";
        return 1;
    }
    return 0;
}
