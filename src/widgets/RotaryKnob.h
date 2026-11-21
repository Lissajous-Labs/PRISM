#pragma once

#include "widgets/WidgetCore.h"

namespace prism::widgets {
class RotaryKnob {
  public:
    static constexpr float sweepDegrees = 135;
    static constexpr float dragPixels = 200;

    RotaryKnob(BoundParameter parameter, math::Vector2 center, float radius) noexcept
        : parameter_(parameter), center_(center), radius_(radius) {}

    bool handle(const PointerEvent &event);
    void paint(render::DrawList &list, const WidgetColors &colors) const;

    [[nodiscard]] bool dragging() const noexcept { return dragging_; }
    [[nodiscard]] float angleDegrees() const noexcept {
        return -sweepDegrees + 2 * sweepDegrees * parameter_.value();
    }
    [[nodiscard]] bool hit(math::Vector2 p) const noexcept {
        return math::length(p - center_) <= radius_;
    }

  private:
    BoundParameter parameter_;
    math::Vector2 center_;
    float radius_;
    bool dragging_ = false;
    float anchorY_ = 0;
    float anchorValue_ = 0;
};
}
