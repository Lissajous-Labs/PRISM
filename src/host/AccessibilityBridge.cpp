#include "host/AccessibilityBridge.h"

#include <algorithm>
#include <array>
#include <utility>

namespace prism::host {
namespace {
constexpr std::array<std::pair<std::string_view, AccessibleRole>, 9> roleByType{{
    {"Knob", AccessibleRole::slider},
    {"Slider", AccessibleRole::slider},
    {"XYPad", AccessibleRole::slider},
    {"Button", AccessibleRole::button},
    {"Toggle", AccessibleRole::toggle},
    {"ComboBox", AccessibleRole::menu},
    {"Label", AccessibleRole::staticText},
    {"NodeCard", AccessibleRole::card},
    {"Editor", AccessibleRole::group},
}};

std::optional<AccessibleRole> roleFor(std::string_view type) {
    for (const auto &[name, role] : roleByType) {
        if (name == type) {
            return role;
        }
    }
    return std::nullopt;
}

std::uint8_t actionsFor(AccessibleRole role) {
    switch (role) {
    case AccessibleRole::slider:
        return actions::increment | actions::decrement;
    case AccessibleRole::button:
        return actions::press;
    case AccessibleRole::toggle:
        return actions::press | actions::toggle;
    case AccessibleRole::menu:
        return actions::press | actions::showMenu;
    default:
        return 0;
    }
}

void collect(const dom::DomDocument &document, const dom::layout::LayoutEngine &layout,
             const ParameterMirror &parameters, dom::NodeId id, std::uint32_t parent,
             std::vector<AccessibleNode> &out) {
    const dom::DomNode &node = document.node(id);
    const auto role = roleFor(document.typeName(node));
    std::uint32_t self = parent;
    if (role && layout.placed(id)) {
        AccessibleNode entry;
        entry.node = id;
        entry.role = *role;
        entry.parent = parent;
        entry.actions = actionsFor(*role);
        entry.bounds = layout.pixels(id);
        if (const auto label = document.attribute(id, "label")) {
            entry.label = *label;
        } else if (node.element != dom::noString) {
            entry.label = document.text(node.element);
        }
        for (const dom::Binding &binding : node.bindings) {
            if (binding.kind == dom::BindingKind::parameter) {
                entry.parameter = parameters.indexOf(document.text(binding.source));
            }
        }
        if (entry.parameter) {
            const ParameterSpec &spec = parameters.spec(*entry.parameter);
            entry.minimum = spec.minimum;
            entry.maximum = spec.maximum;
            entry.step = spec.step;
            entry.value = parameters.format(*entry.parameter);
            if (entry.label.empty()) {
                entry.label = spec.name;
            }
        }
        out.push_back(std::move(entry));
        self = static_cast<std::uint32_t>(out.size() - 1);
    }
    document.forEachChild(
        id, [&](dom::NodeId child) { collect(document, layout, parameters, child, self, out); });
}
}

void AccessibilityBridge::rebuild(const dom::DomDocument &document,
                                  const dom::layout::LayoutEngine &layout,
                                  const ParameterMirror &parameters) {
    nodes_.clear();
    if (document.root().valid()) {
        collect(document, layout, parameters, document.root(), 0xFFFFFFFF, nodes_);
    }
}

void AccessibilityBridge::refreshValues(const ParameterMirror &parameters) {
    for (AccessibleNode &node : nodes_) {
        if (node.parameter) {
            node.value = parameters.format(*node.parameter);
        }
    }
}

bool AccessibilityBridge::perform(std::uint32_t index, std::uint8_t action,
                                  ParameterMirror &parameters, HostParameterSink &sink) {
    if (index >= nodes_.size() || (nodes_[index].actions & action) == 0 ||
        !nodes_[index].parameter) {
        return false;
    }
    AccessibleNode &node = nodes_[index];
    const std::uint32_t parameter = *node.parameter;
    const float current = parameters.normalized(parameter);
    float next = current;
    const float range = node.maximum - node.minimum;
    const float increment = node.step > 0 && range > 0 ? node.step / range : 0.01f;
    if (action == actions::increment) {
        next = std::min(1.0f, current + increment);
    } else if (action == actions::decrement) {
        next = std::max(0.0f, current - increment);
    } else if (action == actions::toggle || action == actions::press) {
        next = current >= 0.5f ? 0.0f : 1.0f;
    } else {
        return true;
    }
    parameters.edit(parameter, next, sink);
    node.value = parameters.format(parameter);
    return true;
}

std::vector<AccessibilityIssue> AccessibilityBridge::validate() const {
    std::vector<AccessibilityIssue> issues;
    for (std::uint32_t i = 0; i < nodes_.size(); ++i) {
        const AccessibleNode &node = nodes_[i];
        const bool interactive = node.actions != 0;
        if (interactive && node.label.empty()) {
            issues.push_back({i, "interactive node without label"});
        }
        if (node.bounds.x1 <= node.bounds.x0 || node.bounds.y1 <= node.bounds.y0) {
            issues.push_back({i, "empty bounds"});
        }
        if (node.role == AccessibleRole::slider &&
            (!node.parameter || node.value.empty() || node.maximum <= node.minimum)) {
            issues.push_back({i, "slider without value range"});
        }
        if (node.parent != 0xFFFFFFFF && node.parent >= i) {
            issues.push_back({i, "parent ordering"});
        }
    }
    return issues;
}
}
