#include "widgets/NodeCard.h"

namespace prism::widgets {
NodeCard::NodeCard(math::Vector2 position, math::Vector2 size, std::span<const Port> ports,
                   std::string_view title)
    : position_(position), size_(size), portCount_(std::min(ports.size(), maxPorts)),
      title_(title) {
    for (std::size_t i = 0; i < portCount_; ++i) {
        ports_[i] = ports[i];
    }
}

render::RectF NodeCard::bounds() const noexcept {
    return {static_cast<float>(position_.x), static_cast<float>(position_.y),
            static_cast<float>(position_.x + size_.x), static_cast<float>(position_.y + size_.y)};
}

std::optional<std::size_t> NodeCard::portAt(math::Vector2 p) const noexcept {
    for (std::size_t i = 0; i < portCount_; ++i) {
        if (math::length(p - portPosition(i)) <= portRadius + 2) {
            return i;
        }
    }
    return std::nullopt;
}

bool NodeCard::handle(const PointerEvent &event) {
    const render::RectF b = bounds();
    switch (event.kind) {
    case PointerKind::down:
        if (!inside({b.x0, b.y0, b.x1, b.y0 + headerHeight}, event.position) ||
            portAt(event.position)) {
            return false;
        }
        dragging_ = true;
        grab_ = event.position - position_;
        return true;
    case PointerKind::move:
        if (!dragging_) {
            return false;
        }
        position_ = event.position - grab_;
        return true;
    case PointerKind::up:
        if (!dragging_) {
            return false;
        }
        dragging_ = false;
        return true;
    default:
        return false;
    }
}

void NodeCard::paint(render::DrawList &list, const WidgetColors &colors, TextPainter text) const {
    const render::RectF b = bounds();
    list.boxShadow(b, 8, 6, render::packColor(0, 0, 0, 110));
    list.roundedRect(b, {8, 8, 8, 8}, colors.surface);
    list.roundedRect({b.x0, b.y0, b.x1, b.y0 + headerHeight}, {8, 0, 8, 0}, colors.track);
    text(list, title_, {(b.x0 + b.x1) / 2, b.y0 + headerHeight / 2}, colors.pointer);
    for (std::size_t i = 0; i < portCount_; ++i) {
        const math::Vector2 p = portPosition(i);
        const float x = static_cast<float>(p.x);
        const float y = static_cast<float>(p.y);
        list.roundedRect({x - portRadius, y - portRadius, x + portRadius, y + portRadius},
                         {portRadius, portRadius, portRadius, portRadius}, colors.ring);
        const float inner = portRadius - 2;
        list.roundedRect({x - inner, y - inner, x + inner, y + inner}, {inner, inner, inner, inner},
                         ports_[i].output ? colors.accent : colors.body);
    }
}
}
