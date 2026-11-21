#pragma once

#include "dom/StyleResolver.h"
#include "dom/StyleVocabulary.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace prism::studio {
enum class Origin : std::uint8_t { initial, rule, inlineStyle, inherited };

struct PropertyTrace {
    dom::Property property;
    Origin origin;
    std::int32_t rule;
    std::uint32_t line;
    std::uint32_t ids;
    std::uint32_t classes;
    std::uint32_t types;
    std::uint32_t inheritedFrom;
};

[[nodiscard]] std::vector<PropertyTrace> traceNode(const dom::StyleResolver &resolver,
                                                   const dom::StyleSheet &sheet, dom::NodeId node);
[[nodiscard]] std::string describeNode(const dom::DomDocument &document,
                                       const dom::StyleResolver &resolver,
                                       const dom::StyleSheet &sheet, dom::NodeId node);
}
