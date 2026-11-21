#include "../support/AllocGuard.h"
#include "../support/Pcg.h"
#include "scene/AnimationClock.h"
#include "scene/SpatialBatcher.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace {
using namespace prism;

struct Item {
    hal::Rect bounds;
    std::uint16_t key;
};

std::vector<Item> randomItems(std::size_t count, std::uint64_t seed, std::uint16_t keys) {
    test::Pcg random(seed);
    std::vector<Item> items(count);
    for (Item &item : items) {
        const auto x = static_cast<std::int32_t>(random.unit() * 1900);
        const auto y = static_cast<std::int32_t>(random.unit() * 1060);
        const auto w = static_cast<std::int32_t>(4 + random.unit() * 60);
        const auto h = static_cast<std::int32_t>(4 + random.unit() * 40);
        item = {{x, y, x + w, y + h}, static_cast<std::uint16_t>(random.next() % keys)};
    }
    return items;
}

bool overlap(const hal::Rect &a, const hal::Rect &b) {
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

bool checkPainterOrder() {
    scene::SpatialBatcher batcher({1920, 1080});
    const auto items = randomItems(3000, 7, 4);
    std::vector<std::uint32_t> assigned(items.size());
    batcher.beginFrame();
    for (std::size_t i = 0; i < items.size(); ++i) {
        assigned[i] = batcher.insert(items[i].bounds, items[i].key);
    }
    for (std::size_t j = 0; j < items.size(); ++j) {
        if (batcher.batches()[assigned[j]].key != items[j].key) {
            return false;
        }
        for (std::size_t i = 0; i < j; ++i) {
            if (overlap(items[i].bounds, items[j].bounds) && assigned[i] > assigned[j]) {
                return false;
            }
        }
    }
    const std::size_t randomBatches = batcher.batches().size();
    batcher.beginFrame();
    for (std::int32_t i = 0; i < 200; ++i) {
        const std::int32_t x = (i % 20) * 96;
        const std::int32_t y = (i / 20) * 96;
        static_cast<void>(
            batcher.insert({x, y, x + 60, y + 60}, static_cast<std::uint16_t>(i % 2)));
    }
    std::cout << "Random scene batches " << randomBatches << " for " << assigned.size()
              << " items, interleaved batches " << batcher.batches().size() << '\n';
    return batcher.batches().size() == 2;
}

bool checkGenerationWrap() {
    scene::SpatialBatcher batcher({256, 256});
    const auto frame = [&] {
        batcher.beginFrame();
        const std::uint32_t a = batcher.insert({10, 10, 40, 40}, 1);
        const std::uint32_t b = batcher.insert({20, 20, 50, 50}, 2);
        const std::uint32_t c = batcher.insert({30, 30, 60, 60}, 1);
        const std::uint32_t d = batcher.insert({200, 200, 220, 220}, 1);
        return a == 0 && b == 1 && c == 2 && d == 2;
    };
    bool stable = frame();
    for (int i = 0; i < 70000; ++i) {
        batcher.beginFrame();
        if (i % 9973 == 0) {
            stable = stable && frame();
        }
    }
    return stable && frame() && batcher.generation() > 0;
}

std::uint64_t checksum_ = 0;

double nanosecondsPerItem(scene::SpatialBatcher &batcher, const std::vector<Item> &items,
                          int frames) {
    double best = 1e30;
    for (int run = 0; run < 5; ++run) {
        const auto start = std::chrono::steady_clock::now();
        std::uint64_t sink = 0;
        for (int f = 0; f < frames; ++f) {
            batcher.beginFrame();
            for (const Item &item : items) {
                sink += batcher.insert(item.bounds, item.key);
            }
        }
        const std::chrono::duration<double, std::nano> elapsed =
            std::chrono::steady_clock::now() - start;
        best = std::min(best, elapsed.count() / (static_cast<double>(frames) * items.size()));
        checksum_ ^= sink;
    }
    return best;
}

bool checkLinearScaling() {
    scene::SpatialBatcher batcher({1920, 1080}, 1u << 16);
    const auto small = randomItems(1000, 11, 3);
    const auto medium = randomItems(10000, 12, 3);
    const auto large = randomItems(100000, 13, 3);
    const double perSmall = nanosecondsPerItem(batcher, small, 100);
    const double perMedium = nanosecondsPerItem(batcher, medium, 10);
    const double perLarge = nanosecondsPerItem(batcher, large, 1);
    test::AllocGuard guard;
    for (int f = 0; f < 4; ++f) {
        batcher.beginFrame();
        for (const Item &item : large) {
            static_cast<void>(batcher.insert(item.bounds, item.key));
        }
    }
    const bool allocationFree = guard.counts().empty();
    std::cout << "Batcher ns per primitive: 1e3 " << perSmall << ", 1e4 " << perMedium << ", 1e5 "
              << perLarge << ", allocation free " << allocationFree << ", checksum "
              << (checksum_ & 0xFF) << '\n';
    return allocationFree && perLarge < 4 * perSmall && perMedium < 4 * perSmall;
}

bool checkAnimationClock() {
    auto clock = std::make_unique<scene::AnimationClock<4096>>();
    clock->set(5, 0);
    clock->set(700, 10);
    clock->set(4000, -3);
    clock->animate(5, 1, 20);
    clock->animate(700, 0, 12);
    clock->animate(4000, 2, 30);
    std::size_t visited = clock->step(1.0 / 120, [](std::size_t, double) {});
    bool sparse = visited == 3;
    const math::SpringState start{0.4, -2};
    const auto whole = math::criticalSpringStep(start, 1, 9, 0.05);
    const auto halves =
        math::criticalSpringStep(math::criticalSpringStep(start, 1, 9, 0.025), 1, 9, 0.025);
    const bool exact = std::abs(whole.position - halves.position) < 1e-12 &&
                       std::abs(whole.velocity - halves.velocity) < 1e-12;
    int frames = 0;
    double overshoot = 0;
    while (clock->running() && frames < 2000) {
        visited = clock->step(1.0 / 120, [&](std::size_t id, double value) {
            if (id == 5) {
                overshoot = std::max(overshoot, value - 1);
            }
        });
        sparse = sparse && visited <= 3;
        ++frames;
    }
    test::AllocGuard guard;
    const std::size_t idle = clock->step(1.0 / 120, [](std::size_t, double) {});
    std::cout << "Springs settled after " << frames << " frames\n";
    return sparse && exact && !clock->running() && idle == 0 && guard.counts().empty() &&
           clock->value(5) == 1 && clock->value(700) == 0 && clock->value(4000) == 2 &&
           overshoot <= 0 && frames < 200;
}
}

int main() {
    if (!checkPainterOrder() || !checkGenerationWrap() || !checkLinearScaling() ||
        !checkAnimationClock()) {
        std::cerr << "Batcher contract failed\n";
        return 1;
    }
    return 0;
}
