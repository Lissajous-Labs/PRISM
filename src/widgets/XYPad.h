#pragma once

#include "widgets/WidgetCore.h"

namespace prism::widgets {
class XYPad {
  public:
    XYPad(BoundParameter x, BoundParameter y, render::RectF bounds) noexcept
        : x_(x), y_(y), bounds_(bounds) {}

    bool handle(const PointerEvent &event);
    void paint(render::DrawList &list, const WidgetColors &colors) const;
    [[nodiscard]] math::Vector2 puck() const noexcept;

  private:
    BoundParameter x_;
    BoundParameter y_;
    render::RectF bounds_;
    bool dragging_ = false;

    void apply(math::Vector2 p) const;
};
}
