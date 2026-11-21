#include "widgets/Slider.h"

namespace prism::widgets {
namespace {
constexpr float thumbHalf = 5;
}

float Slider::valueAt(math::Vector2 p) const noexcept {
    if (vertical_) {
        const float span = bounds_.y1 - bounds_.y0 - 2 * thumbHalf;
        return std::clamp((bounds_.y1 - thumbHalf - static_cast<float>(p.y)) / span, 0.0f, 1.0f);
    }
    const float span = bounds_.x1 - bounds_.x0 - 2 * thumbHalf;
    return std::clamp((static_cast<float>(p.x) - bounds_.x0 - thumbHalf) / span, 0.0f, 1.0f);
}

math::Vector2 Slider::thumb() const noexcept {
    const float v = parameter_.value();
    if (vertical_) {
        return {(bounds_.x0 + bounds_.x1) / 2,
                bounds_.y1 - thumbHalf - v * (bounds_.y1 - bounds_.y0 - 2 * thumbHalf)};
    }
    return {bounds_.x0 + thumbHalf + v * (bounds_.x1 - bounds_.x0 - 2 * thumbHalf),
            (bounds_.y0 + bounds_.y1) / 2};
}

bool Slider::handle(const PointerEvent &event) {
    switch (event.kind) {
    case PointerKind::down: {
        if (!inside(bounds_, event.position)) {
            return false;
        }
        const math::Vector2 t = thumb();
        const double along = vertical_ ? event.position.y - t.y : event.position.x - t.x;
        dragging_ = true;
        parameter_.begin();
        if (std::abs(along) <= thumbHalf) {
            grabOffset_ = static_cast<float>(along);
        } else {
            grabOffset_ = 0;
            parameter_.set(valueAt(event.position));
        }
        return true;
    }
    case PointerKind::move: {
        if (!dragging_) {
            return false;
        }
        math::Vector2 p = event.position;
        (vertical_ ? p.y : p.x) -= grabOffset_;
        parameter_.set(valueAt(p));
        return true;
    }
    case PointerKind::up:
        if (!dragging_) {
            return false;
        }
        dragging_ = false;
        parameter_.end();
        return true;
    case PointerKind::wheel:
        if (!inside(bounds_, event.position)) {
            return false;
        }
        parameter_.set(parameter_.value() + event.wheel * (event.fine ? 0.001f : 0.01f));
        return true;
    case PointerKind::doubleClick:
        if (!inside(bounds_, event.position)) {
            return false;
        }
        parameter_.set(parameter_.defaultValue());
        return true;
    }
    return false;
}

void Slider::paint(render::DrawList &list, const WidgetColors &colors) const {
    const math::Vector2 t = thumb();
    if (vertical_) {
        const float ty = static_cast<float>(t.y);
        const float cx = (bounds_.x0 + bounds_.x1) / 2;
        list.roundedRect({cx - 2.5f, bounds_.y0, cx + 2.5f, bounds_.y1}, {2.5f, 2.5f, 2.5f, 2.5f},
                         colors.track);
        list.roundedRect({cx - 2.5f, ty, cx + 2.5f, bounds_.y1}, {2.5f, 2.5f, 2.5f, 2.5f},
                         colors.accent);
        list.roundedRect({cx - 11, ty - 4.5f, cx + 11, ty + 4.5f}, {4.5f, 4.5f, 4.5f, 4.5f},
                         colors.ring);
        list.roundedRect({cx - 10, ty - 3.5f, cx + 10, ty + 3.5f}, {3.5f, 3.5f, 3.5f, 3.5f},
                         colors.pointer);
        return;
    }
    const float tx = static_cast<float>(t.x);
    const float cy = (bounds_.y0 + bounds_.y1) / 2;
    list.roundedRect({bounds_.x0, cy - 2.5f, bounds_.x1, cy + 2.5f}, {2.5f, 2.5f, 2.5f, 2.5f},
                     colors.track);
    list.roundedRect({bounds_.x0, cy - 2.5f, tx, cy + 2.5f}, {2.5f, 2.5f, 2.5f, 2.5f},
                     colors.accent);
    list.roundedRect({tx - 4.5f, cy - 11, tx + 4.5f, cy + 11}, {4.5f, 4.5f, 4.5f, 4.5f},
                     colors.ring);
    list.roundedRect({tx - 3.5f, cy - 10, tx + 3.5f, cy + 10}, {3.5f, 3.5f, 3.5f, 3.5f},
                     colors.pointer);
}
}
