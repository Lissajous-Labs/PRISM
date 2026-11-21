#include "widgets/RotaryKnob.h"

namespace prism::widgets {
bool RotaryKnob::handle(const PointerEvent &event) {
    switch (event.kind) {
    case PointerKind::down:
        if (!hit(event.position)) {
            return false;
        }
        dragging_ = true;
        anchorY_ = static_cast<float>(event.position.y);
        anchorValue_ = parameter_.value();
        parameter_.begin();
        return true;
    case PointerKind::move:
        if (!dragging_) {
            return false;
        }
        parameter_.set(anchorValue_ + (anchorY_ - static_cast<float>(event.position.y)) /
                                          (dragPixels * (event.fine ? 10.0f : 1.0f)));
        return true;
    case PointerKind::up:
        if (!dragging_) {
            return false;
        }
        dragging_ = false;
        parameter_.end();
        return true;
    case PointerKind::wheel:
        if (!hit(event.position)) {
            return false;
        }
        parameter_.set(parameter_.value() + event.wheel * (event.fine ? 0.001f : 0.01f));
        return true;
    case PointerKind::doubleClick:
        if (!hit(event.position)) {
            return false;
        }
        parameter_.set(parameter_.defaultValue());
        return true;
    }
    return false;
}

void RotaryKnob::paint(render::DrawList &list, const WidgetColors &colors) const {
    const float cx = static_cast<float>(center_.x);
    const float cy = static_cast<float>(center_.y);
    const float r = radius_;
    list.boxShadow({cx - r, cy - r + 2.5f, cx + r, cy + r + 2.5f}, r, 2.6f,
                   render::packColor(0, 0, 0, 190));
    list.roundedRect({cx - r, cy - r, cx + r, cy + r}, {r, r, r, r}, colors.ring);
    const float inner = r - 2.6f;
    list.roundedRect({cx - inner, cy - inner, cx + inner, cy + inner}, {inner, inner, inner, inner},
                     colors.body);
    const float start = screenAngle(-sweepDegrees);
    const float end = screenAngle(angleDegrees());
    if (end > start) {
        list.arc(center_, r - 1.3f, 2.4f, (start + end) / 2, (end - start) / 2, math::ArcCap::butt,
                 colors.accent);
    }
    const math::Vector2 direction{std::cos(end), std::sin(end)};
    list.capsule(center_ + direction * (r * 0.26), center_ + direction * (r * 0.69), 1.05f,
                 colors.pointer);
}
}
