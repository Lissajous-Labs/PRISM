#pragma once

#include "dom/layout/LayoutEngine.h"
#include "render/DrawList.h"
#include "studio/StudioSession.h"

#include <optional>

namespace prism::studio {
class StudioOverlay {
  public:
    explicit StudioOverlay(StudioSession &session) : session_(&session) {}

    [[nodiscard]] dom::NodeId hitTest(const dom::layout::LayoutEngine &layout, float x,
                                      float y) const;
    void hover(dom::NodeId node) noexcept { hovered_ = node; }
    void select(dom::NodeId node) noexcept { selected_ = node; }
    [[nodiscard]] dom::NodeId selected() const noexcept { return selected_; }
    [[nodiscard]] dom::NodeId dragging() const noexcept { return dragging_; }

    bool beginDrag(const dom::layout::LayoutEngine &layout, float x, float y);
    void dragTo(float x, float y);
    void endDrag();

    void draw(render::DrawList &list, const dom::layout::LayoutEngine &layout) const;

  private:
    StudioSession *session_;
    dom::NodeId hovered_{};
    dom::NodeId selected_{};
    dom::NodeId dragging_{};
    float originX_ = 0, originY_ = 0;
    float startX_ = 0, startY_ = 0;

    void deepest(const dom::layout::LayoutEngine &layout, dom::NodeId node, float x, float y,
                 dom::NodeId &best) const;
};
}
