#pragma once

#include "widgets/WidgetCore.h"

#include <span>

namespace prism::widgets {
class ComboBox {
  public:
    static constexpr float itemHeight = 20;

    ComboBox(BoundParameter parameter, render::RectF bounds,
             std::span<const std::string_view> items) noexcept
        : parameter_(parameter), bounds_(bounds), items_(items) {}

    bool handle(const PointerEvent &event);
    void paint(render::DrawList &list, const WidgetColors &colors, TextPainter text) const;

    [[nodiscard]] std::size_t selected() const noexcept;
    [[nodiscard]] bool open() const noexcept { return open_; }
    [[nodiscard]] render::RectF popup() const noexcept;

  private:
    BoundParameter parameter_;
    render::RectF bounds_;
    std::span<const std::string_view> items_;
    bool open_ = false;
    std::size_t hover_ = 0;

    void select(std::size_t index) const;
};
}
