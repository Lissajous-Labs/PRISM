#pragma once

#include "widgets/WidgetCore.h"

namespace prism::widgets {
enum class ButtonMode : std::uint8_t { momentary, toggle };

class Button {
  public:
    Button(BoundParameter parameter, render::RectF bounds, ButtonMode mode,
           std::string_view label) noexcept
        : parameter_(parameter), bounds_(bounds), mode_(mode), label_(label) {}

    bool handle(const PointerEvent &event);
    void paint(render::DrawList &list, const WidgetColors &colors, TextPainter text) const;

    [[nodiscard]] bool on() const noexcept { return parameter_.value() >= 0.5f; }
    [[nodiscard]] bool pressed() const noexcept { return pressed_; }

  private:
    BoundParameter parameter_;
    render::RectF bounds_;
    ButtonMode mode_;
    std::string_view label_;
    bool pressed_ = false;
};
}
