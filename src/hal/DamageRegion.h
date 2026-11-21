#pragma once

#include "core/FunctionRef.h"

#include <cstdint>
#include <span>
#include <vector>

namespace prism::hal {
struct Rect {
    std::int32_t x0;
    std::int32_t y0;
    std::int32_t x1;
    std::int32_t y1;

    [[nodiscard]] constexpr bool empty() const noexcept { return x1 <= x0 || y1 <= y0; }
    friend constexpr bool operator==(const Rect &, const Rect &) = default;
};

struct Span {
    std::int32_t x0;
    std::int32_t x1;
    friend constexpr bool operator==(const Span &, const Span &) = default;
};

struct Band {
    std::int32_t y0;
    std::int32_t y1;
    std::uint32_t firstSpan;
    std::uint32_t spanCount;
};

enum class RegionOp { unite, intersect, subtract };

class DamageRegion {
  public:
    DamageRegion() = default;
    explicit DamageRegion(Rect rect) { assign(rect); }

    void clear() noexcept;
    void assign(Rect rect);
    void add(Rect rect);
    void apply(RegionOp op, const DamageRegion &other);
    void clip(Rect bounds);
    void dilate(std::int32_t radius);

    [[nodiscard]] bool empty() const noexcept { return bands_.empty(); }
    [[nodiscard]] std::int64_t area() const noexcept;
    [[nodiscard]] Rect bounds() const noexcept;
    [[nodiscard]] bool contains(std::int32_t x, std::int32_t y) const noexcept;
    [[nodiscard]] std::span<const Band> bands() const noexcept { return bands_; }
    [[nodiscard]] std::span<const Span> spans(const Band &band) const noexcept {
        return {spans_.data() + band.firstSpan, band.spanCount};
    }
    void forEachRect(FunctionRef<void(Rect)> visitor) const;

    static void combine(const DamageRegion &first, const DamageRegion &second, RegionOp op,
                        DamageRegion &output);

  private:
    std::vector<Band> bands_;
    std::vector<Span> spans_;
    std::vector<Band> scratchBands_;
    std::vector<Span> scratchSpans_;

    void combineInto(const DamageRegion &first, const DamageRegion &second, RegionOp op);
};
}
