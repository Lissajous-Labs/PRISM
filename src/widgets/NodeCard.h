#pragma once

#include "widgets/WidgetCore.h"

#include <array>
#include <optional>
#include <span>

namespace prism::widgets {
struct Port {
    math::Vector2 offset;
    bool output;
};

class NodeCard {
  public:
    static constexpr std::size_t maxPorts = 16;
    static constexpr float headerHeight = 24;
    static constexpr float portRadius = 6;

    NodeCard(math::Vector2 position, math::Vector2 size, std::span<const Port> ports,
             std::string_view title);

    bool handle(const PointerEvent &event);
    void paint(render::DrawList &list, const WidgetColors &colors, TextPainter text) const;

    [[nodiscard]] std::optional<std::size_t> portAt(math::Vector2 p) const noexcept;
    [[nodiscard]] math::Vector2 portPosition(std::size_t port) const noexcept {
        return position_ + ports_[port].offset;
    }
    [[nodiscard]] math::Vector2 position() const noexcept { return position_; }
    [[nodiscard]] render::RectF bounds() const noexcept;
    [[nodiscard]] bool dragging() const noexcept { return dragging_; }

  private:
    math::Vector2 position_;
    math::Vector2 size_;
    std::array<Port, maxPorts> ports_{};
    std::size_t portCount_ = 0;
    std::string_view title_;
    bool dragging_ = false;
    math::Vector2 grab_{0, 0};
};
}
