#include "widgets/XYPad.h"

namespace prism::widgets {
math::Vector2 XYPad::puck() const noexcept {
    return {bounds_.x0 + x_.value() * (bounds_.x1 - bounds_.x0),
            bounds_.y1 - y_.value() * (bounds_.y1 - bounds_.y0)};
}

void XYPad::apply(math::Vector2 p) const {
    x_.set((static_cast<float>(p.x) - bounds_.x0) / (bounds_.x1 - bounds_.x0));
    y_.set((bounds_.y1 - static_cast<float>(p.y)) / (bounds_.y1 - bounds_.y0));
}

bool XYPad::handle(const PointerEvent &event) {
    switch (event.kind) {
    case PointerKind::down:
        if (!inside(bounds_, event.position)) {
            return false;
        }
        dragging_ = true;
        x_.begin();
        y_.begin();
        apply(event.position);
        return true;
    case PointerKind::move:
        if (!dragging_) {
            return false;
        }
        apply(event.position);
        return true;
    case PointerKind::up:
        if (!dragging_) {
            return false;
        }
        dragging_ = false;
        x_.end();
        y_.end();
        return true;
    case PointerKind::doubleClick:
        if (!inside(bounds_, event.position)) {
            return false;
        }
        x_.set(x_.defaultValue());
        y_.set(y_.defaultValue());
        return true;
    default:
        return false;
    }
}

void XYPad::paint(render::DrawList &list, const WidgetColors &colors) const {
    list.roundedRect(bounds_, {6, 6, 6, 6}, colors.track);
    const math::Vector2 p = puck();
    const float px = static_cast<float>(p.x);
    const float py = static_cast<float>(p.y);
    list.roundedRect({px - 0.5f, bounds_.y0 + 2, px + 0.5f, bounds_.y1 - 2}, {}, colors.surface);
    list.roundedRect({bounds_.x0 + 2, py - 0.5f, bounds_.x1 - 2, py + 0.5f}, {}, colors.surface);
    list.roundedRect({px - 7, py - 7, px + 7, py + 7}, {7, 7, 7, 7}, colors.ring);
    list.roundedRect({px - 5.5f, py - 5.5f, px + 5.5f, py + 5.5f}, {5.5f, 5.5f, 5.5f, 5.5f},
                     colors.accent);
}
}
