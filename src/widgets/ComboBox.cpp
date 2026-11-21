#include "widgets/ComboBox.h"

namespace prism::widgets {
std::size_t ComboBox::selected() const noexcept {
    if (items_.size() < 2) {
        return 0;
    }
    return std::min(items_.size() - 1,
                    static_cast<std::size_t>(
                        std::lround(parameter_.value() * static_cast<float>(items_.size() - 1))));
}

render::RectF ComboBox::popup() const noexcept {
    return {bounds_.x0, bounds_.y1 + 2, bounds_.x1,
            bounds_.y1 + 2 + itemHeight * static_cast<float>(items_.size())};
}

void ComboBox::select(std::size_t index) const {
    const float denominator = items_.size() > 1 ? static_cast<float>(items_.size() - 1) : 1.0f;
    parameter_.set(static_cast<float>(index) / denominator);
}

bool ComboBox::handle(const PointerEvent &event) {
    const render::RectF list = popup();
    switch (event.kind) {
    case PointerKind::down:
        if (open_) {
            open_ = false;
            if (inside(list, event.position)) {
                select(static_cast<std::size_t>((event.position.y - list.y0) / itemHeight));
            }
            return true;
        }
        if (!inside(bounds_, event.position)) {
            return false;
        }
        open_ = true;
        hover_ = selected();
        return true;
    case PointerKind::move:
        if (open_ && inside(list, event.position)) {
            hover_ = static_cast<std::size_t>((event.position.y - list.y0) / itemHeight);
            return true;
        }
        return false;
    case PointerKind::wheel: {
        if (open_ || !inside(bounds_, event.position) || items_.empty()) {
            return false;
        }
        const auto current = static_cast<long>(selected());
        const long next = std::clamp(current + (event.wheel > 0 ? -1L : 1L), 0L,
                                     static_cast<long>(items_.size()) - 1);
        select(static_cast<std::size_t>(next));
        return true;
    }
    default:
        return false;
    }
}

void ComboBox::paint(render::DrawList &list, const WidgetColors &colors, TextPainter text) const {
    list.roundedRect(bounds_, {6, 6, 6, 6}, colors.surface);
    const float cy = (bounds_.y0 + bounds_.y1) / 2;
    if (!items_.empty()) {
        text(list, items_[selected()], {(bounds_.x0 + bounds_.x1) / 2 - 6, cy}, colors.text);
    }
    list.triangle({bounds_.x1 - 15.6f, cy - 1.9f}, {bounds_.x1 - 8.4f, cy - 1.9f},
                  {bounds_.x1 - 12, cy + 2.1f}, 0.4f, colors.text);
    if (!open_) {
        return;
    }
    const render::RectF menu = popup();
    list.boxShadow(menu, 6, 6, render::packColor(0, 0, 0, 120));
    list.roundedRect(menu, {6, 6, 6, 6}, colors.track);
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const float y0 = menu.y0 + itemHeight * static_cast<float>(i);
        if (i == hover_) {
            list.roundedRect({menu.x0 + 2, y0 + 1, menu.x1 - 2, y0 + itemHeight - 1}, {4, 4, 4, 4},
                             colors.accent);
        }
        text(list, items_[i], {(menu.x0 + menu.x1) / 2, y0 + itemHeight / 2},
             i == hover_ ? colors.text : colors.pointer);
    }
}
}
