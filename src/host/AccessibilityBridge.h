#pragma once

#include "dom/DomDocument.h"
#include "dom/layout/LayoutEngine.h"
#include "host/ProcessorState.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace prism::host {
enum class AccessibleRole : std::uint8_t { group, slider, button, toggle, menu, staticText, card };

namespace actions {
inline constexpr std::uint8_t press = 1, increment = 2, decrement = 4, toggle = 8, showMenu = 16;
}

struct AccessibleNode {
    dom::NodeId node;
    AccessibleRole role = AccessibleRole::group;
    std::string label;
    std::string value;
    std::optional<std::uint32_t> parameter;
    float minimum = 0;
    float maximum = 0;
    float step = 0;
    std::uint8_t actions = 0;
    dom::layout::PixelRect bounds;
    std::uint32_t parent = 0xFFFFFFFF;
};

struct AccessibilityIssue {
    std::uint32_t node;
    std::string message;
};

class AccessibilityBridge {
  public:
    void rebuild(const dom::DomDocument &document, const dom::layout::LayoutEngine &layout,
                 const ParameterMirror &parameters);
    void refreshValues(const ParameterMirror &parameters);
    bool perform(std::uint32_t index, std::uint8_t action, ParameterMirror &parameters,
                 HostParameterSink &sink);

    [[nodiscard]] const std::vector<AccessibleNode> &nodes() const noexcept { return nodes_; }
    [[nodiscard]] std::vector<AccessibilityIssue> validate() const;

  private:
    std::vector<AccessibleNode> nodes_;
};
}
