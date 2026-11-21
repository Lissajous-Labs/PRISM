#pragma once

#include "core/FunctionRef.h"
#include "dom/DomDocument.h"
#include "dom/StyleResolver.h"
#include "dom/layout/LayoutSolvers.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace prism::dom::layout {
struct LayoutBox {
    float x0 = 0;
    float y0 = 0;
    float x1 = 0;
    float y1 = 0;

    [[nodiscard]] float width() const noexcept { return x1 - x0; }
    [[nodiscard]] float height() const noexcept { return y1 - y0; }
};

struct PixelRect {
    std::int64_t x0 = 0;
    std::int64_t y0 = 0;
    std::int64_t x1 = 0;
    std::int64_t y1 = 0;
};

struct ContentSize {
    float width = 0;
    float height = 0;
};

using MeasureFunction = FunctionRef<ContentSize(NodeId, float)>;

struct ScrollInfo {
    float extent = 0;
    float offset = 0;
    float reservation = 0;
};

struct Viewport {
    float width;
    float height;
    double scale = 1;
};

class LayoutEngine {
  public:
    void layout(const DomDocument &document, const StyleResolver &styles, Viewport viewport);
    void layout(const DomDocument &document, const StyleResolver &styles, Viewport viewport,
                MeasureFunction measure);

    [[nodiscard]] const LayoutBox &box(NodeId id) const noexcept { return boxes_[id.index]; }
    [[nodiscard]] const PixelRect &pixels(NodeId id) const noexcept { return pixels_[id.index]; }
    [[nodiscard]] const ScrollInfo &scroll(NodeId id) const noexcept { return scroll_[id.index]; }
    [[nodiscard]] bool placed(NodeId id) const noexcept {
        return id.index < placed_.size() && placed_[id.index];
    }

  private:
    const DomDocument *document_ = nullptr;
    const StyleResolver *styles_ = nullptr;
    const MeasureFunction *measure_ = nullptr;
    Viewport viewport_{0, 0, 1};
    std::vector<LayoutBox> boxes_;
    std::vector<PixelRect> pixels_;
    std::vector<bool> placed_;
    std::vector<ScrollInfo> scroll_;
    FlexResult flex_;
    GridResult grid_;

    [[nodiscard]] std::optional<float> resolve(const Length &length, float reference,
                                               float fontSize) const;
    [[nodiscard]] ContentSize measure(NodeId id, float maxWidth) const;
    void children(NodeId id, std::vector<NodeId> &out) const;
    void layoutNode(NodeId id, LayoutBox box);
    void layoutFlex(NodeId id, const LayoutBox &content, const std::vector<NodeId> &items);
    void layoutGrid(NodeId id, const LayoutBox &content, const std::vector<NodeId> &items);
    void layoutAnchored(const LayoutBox &content, const std::vector<NodeId> &items);
    void layoutTabbed(NodeId id, const LayoutBox &content, const std::vector<NodeId> &items);
    void layoutScroll(NodeId id, const LayoutBox &content, const std::vector<NodeId> &items);
    void layoutCanvas(NodeId id, const LayoutBox &content, const std::vector<NodeId> &items);
    [[nodiscard]] float numberAttribute(NodeId id, std::string_view name, float fallback) const;
    void snap();
};
}
