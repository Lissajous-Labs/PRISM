#pragma once

#include "widgets/WidgetCore.h"

namespace prism::widgets {
class Slider {
  public:
    Slider(BoundParameter parameter, render::RectF bounds, bool vertical) noexcept
        : parameter_(parameter), bounds_(bounds), vertical_(vertical) {}

    bool handle(const PointerEvent &event);
    void paint(render::DrawList &list, const WidgetColors &colors) const;

    [[nodiscard]] float valueAt(math::Vector2 p) const noexcept;
    [[nodiscard]] math::Vector2 thumb() const noexcept;
    [[nodiscard]] bool dragging() const noexcept { return dragging_; }

  private:
    BoundParameter parameter_;
    render::RectF bounds_;
    bool vertical_;
    bool dragging_ = false;
    float grabOffset_ = 0;
};
}
