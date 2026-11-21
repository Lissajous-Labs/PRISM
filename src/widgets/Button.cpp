#include "widgets/Button.h"

namespace prism::widgets {
bool Button::handle(const PointerEvent &event) {
    if (event.kind == PointerKind::down) {
        if (!inside(bounds_, event.position)) {
            return false;
        }
        pressed_ = true;
        parameter_.begin();
        parameter_.set(mode_ == ButtonMode::toggle ? (on() ? 0.0f : 1.0f) : 1.0f);
        return true;
    }
    if (event.kind == PointerKind::up && pressed_) {
        pressed_ = false;
        if (mode_ == ButtonMode::momentary) {
            parameter_.set(0);
        }
        parameter_.end();
        return true;
    }
    return false;
}

void Button::paint(render::DrawList &list, const WidgetColors &colors, TextPainter text) const {
    const bool lit = on();
    list.roundedRect(bounds_, {6, 6, 6, 6}, lit ? colors.accent : colors.surface);
    if (pressed_) {
        list.roundedRect(bounds_, {6, 6, 6, 6}, render::packColor(0, 0, 0, 40));
    }
    text(list, label_, {(bounds_.x0 + bounds_.x1) / 2, (bounds_.y0 + bounds_.y1) / 2}, colors.text);
}
}
