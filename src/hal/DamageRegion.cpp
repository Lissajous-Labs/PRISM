#include "hal/DamageRegion.h"

#include <algorithm>
#include <limits>

namespace prism::hal {
namespace {
constexpr std::int32_t infinity = std::numeric_limits<std::int32_t>::max();

bool inside(RegionOp op, bool first, bool second) noexcept {
    switch (op) {
    case RegionOp::unite:
        return first || second;
    case RegionOp::intersect:
        return first && second;
    case RegionOp::subtract:
        return first && !second;
    }
    return false;
}

struct EdgeCursor {
    std::span<const Span> spans;
    std::size_t edge = 0;

    [[nodiscard]] std::int32_t position() const noexcept {
        if (edge >= spans.size() * 2) {
            return infinity;
        }
        const Span &span = spans[edge / 2];
        return edge % 2 == 0 ? span.x0 : span.x1;
    }
    [[nodiscard]] bool insideAfter() const noexcept { return edge % 2 == 1; }
};

void combineSpans(std::span<const Span> first, std::span<const Span> second, RegionOp op,
                  std::vector<Span> &output) {
    EdgeCursor a{first};
    EdgeCursor b{second};
    bool inA = false;
    bool inB = false;
    bool active = false;
    std::int32_t start = 0;
    while (true) {
        const std::int32_t x = std::min(a.position(), b.position());
        if (x == infinity) {
            break;
        }
        while (a.position() == x) {
            ++a.edge;
            inA = a.insideAfter();
        }
        while (b.position() == x) {
            ++b.edge;
            inB = b.insideAfter();
        }
        const bool now = inside(op, inA, inB);
        if (now && !active) {
            start = x;
        } else if (!now && active) {
            output.push_back({start, x});
        }
        active = now;
    }
}

struct BandCursor {
    std::span<const Band> bands;
    std::size_t index = 0;

    void advance(std::int32_t y) noexcept {
        while (index < bands.size() && bands[index].y1 <= y) {
            ++index;
        }
    }
    [[nodiscard]] const Band *active(std::int32_t y) const noexcept {
        return index < bands.size() && bands[index].y0 <= y ? &bands[index] : nullptr;
    }
    [[nodiscard]] std::int32_t nextEdge(std::int32_t y) const noexcept {
        if (index >= bands.size()) {
            return infinity;
        }
        return bands[index].y0 > y ? bands[index].y0 : bands[index].y1;
    }
};
}

void DamageRegion::clear() noexcept {
    bands_.clear();
    spans_.clear();
}

void DamageRegion::assign(Rect rect) {
    clear();
    if (!rect.empty()) {
        spans_.push_back({rect.x0, rect.x1});
        bands_.push_back({rect.y0, rect.y1, 0, 1});
    }
}

void DamageRegion::add(Rect rect) {
    if (rect.empty()) {
        return;
    }
    if (empty()) {
        assign(rect);
        return;
    }
    DamageRegion single(rect);
    combineInto(*this, single, RegionOp::unite);
}

void DamageRegion::apply(RegionOp op, const DamageRegion &other) { combineInto(*this, other, op); }

void DamageRegion::clip(Rect bounds) { apply(RegionOp::intersect, DamageRegion(bounds)); }

void DamageRegion::dilate(std::int32_t radius) {
    if (radius <= 0 || empty()) {
        return;
    }
    DamageRegion grown;
    forEachRect([&](Rect rect) {
        grown.add({rect.x0 - radius, rect.y0 - radius, rect.x1 + radius, rect.y1 + radius});
    });
    std::swap(bands_, grown.bands_);
    std::swap(spans_, grown.spans_);
}

std::int64_t DamageRegion::area() const noexcept {
    std::int64_t total = 0;
    for (const Band &band : bands_) {
        for (const Span &span : spans(band)) {
            total += static_cast<std::int64_t>(span.x1 - span.x0) * (band.y1 - band.y0);
        }
    }
    return total;
}

Rect DamageRegion::bounds() const noexcept {
    if (empty()) {
        return {0, 0, 0, 0};
    }
    Rect result{infinity, bands_.front().y0, -infinity, bands_.back().y1};
    for (const Band &band : bands_) {
        const auto row = spans(band);
        result.x0 = std::min(result.x0, row.front().x0);
        result.x1 = std::max(result.x1, row.back().x1);
    }
    return result;
}

bool DamageRegion::contains(std::int32_t x, std::int32_t y) const noexcept {
    for (const Band &band : bands_) {
        if (y >= band.y0 && y < band.y1) {
            for (const Span &span : spans(band)) {
                if (x >= span.x0 && x < span.x1) {
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

void DamageRegion::forEachRect(FunctionRef<void(Rect)> visitor) const {
    for (const Band &band : bands_) {
        for (const Span &span : spans(band)) {
            visitor({span.x0, band.y0, span.x1, band.y1});
        }
    }
}

void DamageRegion::combine(const DamageRegion &first, const DamageRegion &second, RegionOp op,
                           DamageRegion &output) {
    output.combineInto(first, second, op);
}

void DamageRegion::combineInto(const DamageRegion &first, const DamageRegion &second, RegionOp op) {
    scratchBands_.clear();
    scratchSpans_.clear();
    BandCursor a{first.bands_};
    BandCursor b{second.bands_};
    std::int32_t y = std::min(a.nextEdge(-infinity), b.nextEdge(-infinity));
    while (y != infinity) {
        a.advance(y);
        b.advance(y);
        const std::int32_t next = std::min(a.nextEdge(y), b.nextEdge(y));
        if (next == infinity) {
            break;
        }
        const Band *bandA = a.active(y);
        const Band *bandB = b.active(y);
        const auto spansA = bandA ? first.spans(*bandA) : std::span<const Span>{};
        const auto spansB = bandB ? second.spans(*bandB) : std::span<const Span>{};
        const auto begin = static_cast<std::uint32_t>(scratchSpans_.size());
        combineSpans(spansA, spansB, op, scratchSpans_);
        const auto count = static_cast<std::uint32_t>(scratchSpans_.size()) - begin;
        if (count != 0) {
            Band *previous = scratchBands_.empty() ? nullptr : &scratchBands_.back();
            const bool same = previous && previous->y1 == y && previous->spanCount == count &&
                              std::equal(scratchSpans_.begin() + previous->firstSpan,
                                         scratchSpans_.begin() + previous->firstSpan + count,
                                         scratchSpans_.begin() + begin);
            if (same) {
                previous->y1 = next;
                scratchSpans_.resize(begin);
            } else {
                scratchBands_.push_back({y, next, begin, count});
            }
        }
        y = next;
    }
    std::swap(bands_, scratchBands_);
    std::swap(spans_, scratchSpans_);
}
}
