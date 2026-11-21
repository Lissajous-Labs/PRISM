#include "../support/AllocGuard.h"
#include "../support/Pcg.h"
#include "hal/BufferAgeHistory.h"
#include "hal/DamageRegion.h"

#include <array>
#include <bitset>
#include <iostream>

namespace {
using namespace prism::hal;
constexpr int grid = 64;
using Mask = std::bitset<grid * grid>;

Mask rasterize(const DamageRegion &region) {
    Mask mask;
    region.forEachRect([&](Rect rect) {
        for (int y = std::max(rect.y0, 0); y < std::min(rect.y1, grid); ++y) {
            for (int x = std::max(rect.x0, 0); x < std::min(rect.x1, grid); ++x) {
                mask.set(static_cast<std::size_t>(y * grid + x));
            }
        }
    });
    return mask;
}

Mask rasterize(Rect rect) { return rasterize(DamageRegion(rect)); }

bool canonical(const DamageRegion &region) {
    const auto bands = region.bands();
    for (std::size_t i = 0; i < bands.size(); ++i) {
        const auto spans = region.spans(bands[i]);
        if (bands[i].y0 >= bands[i].y1 || spans.empty()) {
            return false;
        }
        for (std::size_t s = 0; s < spans.size(); ++s) {
            if (spans[s].x0 >= spans[s].x1 || (s > 0 && spans[s - 1].x1 >= spans[s].x0)) {
                return false;
            }
        }
        if (i > 0) {
            const auto previous = region.spans(bands[i - 1]);
            if (bands[i - 1].y1 > bands[i].y0) {
                return false;
            }
            if (bands[i - 1].y1 == bands[i].y0 &&
                std::equal(previous.begin(), previous.end(), spans.begin(), spans.end())) {
                return false;
            }
        }
    }
    return true;
}

Rect randomRect(prism::test::Pcg &random) {
    const auto x0 = static_cast<std::int32_t>(random.next() % grid);
    const auto y0 = static_cast<std::int32_t>(random.next() % grid);
    const auto width = static_cast<std::int32_t>(random.next() % 24);
    const auto height = static_cast<std::int32_t>(random.next() % 24);
    return {x0, y0, std::min(x0 + width, grid), std::min(y0 + height, grid)};
}

bool checkAlgebra(prism::test::Pcg &random) {
    for (int sequence = 0; sequence < 10000; ++sequence) {
        DamageRegion first;
        DamageRegion second;
        Mask maskFirst;
        Mask maskSecond;
        const int count = 1 + static_cast<int>(random.next() % 6);
        for (int i = 0; i < count; ++i) {
            const Rect a = randomRect(random);
            const Rect b = randomRect(random);
            first.add(a);
            second.add(b);
            maskFirst |= rasterize(a);
            maskSecond |= rasterize(b);
        }
        DamageRegion united;
        DamageRegion intersected;
        DamageRegion subtracted;
        DamageRegion::combine(first, second, RegionOp::unite, united);
        DamageRegion::combine(first, second, RegionOp::intersect, intersected);
        DamageRegion::combine(first, second, RegionOp::subtract, subtracted);
        const Rect clip{8, 8, 40, 40};
        DamageRegion clipped = first;
        clipped.clip(clip);
        if (rasterize(first) != maskFirst || rasterize(united) != (maskFirst | maskSecond) ||
            rasterize(intersected) != (maskFirst & maskSecond) ||
            rasterize(subtracted) != (maskFirst & ~maskSecond) ||
            rasterize(clipped) != (maskFirst & rasterize(clip)) ||
            united.area() != static_cast<std::int64_t>((maskFirst | maskSecond).count()) ||
            !canonical(united) || !canonical(intersected) || !canonical(subtracted)) {
            std::cerr << "Region algebra mismatch at sequence " << sequence << '\n';
            return false;
        }
    }
    return true;
}

bool checkDilationAndHistory() {
    DamageRegion region({10, 10, 12, 12});
    region.add({30, 30, 31, 31});
    region.dilate(3);
    Mask expected = rasterize(Rect{7, 7, 15, 15}) | rasterize(Rect{27, 27, 34, 34});
    if (rasterize(region) != expected || region.contains(20, 20) || !region.contains(7, 7)) {
        return false;
    }
    BufferAgeHistory history;
    DamageRegion repair;
    const Rect viewport{0, 0, 64, 64};
    history.push(DamageRegion({0, 0, 4, 4}));
    history.push(DamageRegion({60, 60, 64, 64}));
    history.repairRegion(2, viewport, repair);
    if (repair.area() != 32 || !repair.contains(0, 0) || !repair.contains(63, 63)) {
        return false;
    }
    history.repairRegion(1, viewport, repair);
    if (repair.area() != 16 || repair.contains(0, 0)) {
        return false;
    }
    history.repairRegion(0, viewport, repair);
    const bool full = repair.area() == 64 * 64;
    history.repairRegion(3, viewport, repair);
    return full && repair.area() == 64 * 64;
}

bool checkSteadyStateAllocations() {
    DamageRegion region;
    DamageRegion other({5, 5, 50, 50});
    for (int warm = 0; warm < 4; ++warm) {
        region.clear();
        region.add({0, 0, 10, 10});
        region.add({20, 20, 30, 30});
        region.apply(RegionOp::subtract, other);
    }
    prism::test::AllocGuard guard;
    for (int frame = 0; frame < 1000; ++frame) {
        region.clear();
        region.apply(RegionOp::unite, other);
        region.apply(RegionOp::intersect, other);
    }
    return guard.counts().empty();
}
}

int main() {
    prism::test::Pcg random(53);
    if (!checkAlgebra(random) || !checkDilationAndHistory() || !checkSteadyStateAllocations()) {
        std::cerr << "Damage region contract failed\n";
        return 1;
    }
    return 0;
}
