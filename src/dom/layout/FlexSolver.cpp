#include "dom/layout/LayoutSolvers.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace prism::dom::layout {
namespace {
struct Line {
    std::size_t first;
    std::size_t count;
    float cross = 0;
    float crossStart = 0;
};

float clampSize(float value, float low, float high) { return std::max(low, std::min(value, high)); }

std::size_t resolveFlexible(const FlexContainer &container, std::span<const FlexItem> items,
                            std::span<const std::size_t> order, std::vector<float> &target) {
    const std::size_t n = order.size();
    std::vector<bool> frozen(n, false);
    float fixedSpace = container.mainGap * static_cast<float>(n > 0 ? n - 1 : 0);
    float hypothetical = fixedSpace;
    for (std::size_t k = 0; k < n; ++k) {
        const FlexItem &item = items[order[k]];
        fixedSpace += 2 * item.marginMain;
        hypothetical += 2 * item.marginMain + clampSize(item.basis, item.minMain, item.maxMain);
    }
    const bool growing = hypothetical < container.mainSize;
    for (std::size_t k = 0; k < n; ++k) {
        const FlexItem &item = items[order[k]];
        const float hyp = clampSize(item.basis, item.minMain, item.maxMain);
        target[k] = item.basis;
        if ((growing ? item.grow == 0 : item.shrink == 0) ||
            (growing ? item.basis > hyp : item.basis < hyp)) {
            frozen[k] = true;
            target[k] = hyp;
        }
    }
    const auto occupied = [&] {
        float sum = fixedSpace;
        for (std::size_t k = 0; k < n; ++k) {
            sum += frozen[k] ? target[k] : items[order[k]].basis;
        }
        return sum;
    };
    const float initialFree = container.mainSize - occupied();
    std::size_t iterations = 0;
    while (std::find(frozen.begin(), frozen.end(), false) != frozen.end()) {
        ++iterations;
        float remaining = container.mainSize - occupied();
        float sumFactors = 0;
        float sumScaled = 0;
        for (std::size_t k = 0; k < n; ++k) {
            if (!frozen[k]) {
                const FlexItem &item = items[order[k]];
                sumFactors += growing ? item.grow : item.shrink;
                sumScaled += item.shrink * item.basis;
            }
        }
        if (sumFactors < 1) {
            const float scaled = initialFree * sumFactors;
            if (std::abs(scaled) < std::abs(remaining)) {
                remaining = scaled;
            }
        }
        float violation = 0;
        std::vector<float> delta(n, 0);
        for (std::size_t k = 0; k < n; ++k) {
            if (frozen[k]) {
                continue;
            }
            const FlexItem &item = items[order[k]];
            float size = item.basis;
            if (growing && sumFactors > 0) {
                size += remaining * item.grow / sumFactors;
            } else if (!growing && sumScaled > 0) {
                size -= std::abs(remaining) * item.shrink * item.basis / sumScaled;
            }
            const float clamped = clampSize(size, item.minMain, item.maxMain);
            delta[k] = clamped - size;
            violation += delta[k];
            target[k] = clamped;
        }
        for (std::size_t k = 0; k < n; ++k) {
            if (!frozen[k] && (violation == 0 || (violation > 0 && delta[k] > 0) ||
                               (violation < 0 && delta[k] < 0))) {
                frozen[k] = true;
            }
        }
    }
    return iterations;
}

void distribute(Keyword mode, float free, std::size_t count, float &offset, float &spacing) {
    offset = 0;
    spacing = 0;
    const auto slots = static_cast<float>(count);
    switch (mode) {
    case Keyword::end:
        offset = free;
        break;
    case Keyword::center:
        offset = free / 2;
        break;
    case Keyword::spaceBetween:
        if (free > 0 && count > 1) {
            spacing = free / (slots - 1);
        }
        break;
    case Keyword::spaceAround:
        if (free > 0) {
            spacing = free / slots;
            offset = spacing / 2;
        } else {
            offset = free / 2;
        }
        break;
    case Keyword::spaceEvenly:
        if (free > 0) {
            spacing = free / (slots + 1);
            offset = spacing;
        } else {
            offset = free / 2;
        }
        break;
    default:
        break;
    }
}
}

void solveFlex(const FlexContainer &container, std::span<const FlexItem> items,
               FlexResult &result) {
    const std::size_t count = items.size();
    result.items.assign(count, Placement{});
    result.lines = 0;
    result.freezeIterations = 0;
    result.usedCross = 0;
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return items[a].order < items[b].order; });
    std::vector<Line> lines;
    std::size_t start = 0;
    float used = 0;
    for (std::size_t k = 0; k < count; ++k) {
        const FlexItem &item = items[order[k]];
        const float outer = clampSize(item.basis, item.minMain, item.maxMain) + 2 * item.marginMain;
        const float needed = (k > start ? used + container.mainGap : 0) + outer;
        if (container.wrap && k > start && needed > container.mainSize) {
            lines.push_back({start, k - start});
            start = k;
            used = outer;
        } else {
            used = needed;
        }
    }
    if (count > 0) {
        lines.push_back({start, count - start});
    }
    std::vector<float> mainSizes(count);
    std::vector<float> crossSizes(count);
    for (Line &line : lines) {
        const std::span<const std::size_t> members(order.data() + line.first, line.count);
        std::vector<float> target(line.count);
        result.freezeIterations += resolveFlexible(container, items, members, target);
        for (std::size_t k = 0; k < line.count; ++k) {
            const FlexItem &item = items[members[k]];
            mainSizes[line.first + k] = target[k];
            const float cross = clampSize(item.cross.value_or(0), item.minCross, item.maxCross);
            crossSizes[line.first + k] = cross;
            line.cross = std::max(line.cross, cross + 2 * item.marginCross);
        }
    }
    const bool definiteCross = std::isfinite(container.crossSize);
    if (lines.size() == 1 && definiteCross) {
        lines[0].cross = container.crossSize;
    }
    float linesTotal =
        container.crossGap * static_cast<float>(lines.empty() ? 0 : lines.size() - 1);
    for (const Line &line : lines) {
        linesTotal += line.cross;
    }
    float crossOffset = 0;
    float crossSpacing = 0;
    if (definiteCross && lines.size() > 1) {
        const float free = container.crossSize - linesTotal;
        if (container.alignContent == Keyword::stretch && free > 0) {
            for (Line &line : lines) {
                line.cross += free / static_cast<float>(lines.size());
            }
        } else {
            distribute(container.alignContent, free, lines.size(), crossOffset, crossSpacing);
        }
    }
    float cursor = crossOffset;
    for (Line &line : lines) {
        line.crossStart = cursor;
        cursor += line.cross + container.crossGap + crossSpacing;
    }
    result.usedCross =
        definiteCross ? container.crossSize : cursor - container.crossGap - crossSpacing;
    for (const Line &line : lines) {
        float occupied = container.mainGap * static_cast<float>(line.count - 1);
        for (std::size_t k = 0; k < line.count; ++k) {
            occupied += mainSizes[line.first + k] + 2 * items[order[line.first + k]].marginMain;
        }
        float offset = 0;
        float spacing = 0;
        distribute(container.justify, container.mainSize - occupied, line.count, offset, spacing);
        float position = offset;
        for (std::size_t k = 0; k < line.count; ++k) {
            const std::size_t index = order[line.first + k];
            const FlexItem &item = items[index];
            Placement &placement = result.items[index];
            placement.mainStart = position + item.marginMain;
            placement.mainEnd = placement.mainStart + mainSizes[line.first + k];
            position = placement.mainEnd + item.marginMain + container.mainGap + spacing;
            const Keyword align =
                item.alignSelf == Keyword::automatic ? container.alignItems : item.alignSelf;
            float cross = crossSizes[line.first + k];
            if (align == Keyword::stretch && !item.cross) {
                cross = clampSize(line.cross - 2 * item.marginCross, item.minCross, item.maxCross);
            }
            float crossStart = line.crossStart + item.marginCross;
            if (align == Keyword::end) {
                crossStart = line.crossStart + line.cross - item.marginCross - cross;
            } else if (align == Keyword::center) {
                crossStart = line.crossStart + (line.cross - cross) / 2;
            }
            placement.crossStart = crossStart;
            placement.crossEnd = crossStart + cross;
            if (container.reverse) {
                const float mirroredStart = container.mainSize - placement.mainEnd;
                placement.mainEnd = container.mainSize - placement.mainStart;
                placement.mainStart = mirroredStart;
            }
        }
    }
    result.lines = lines.size();
}
}
