#include "studio/ProvenanceInspector.h"

namespace prism::studio {
std::vector<PropertyTrace> traceNode(const dom::StyleResolver &resolver,
                                     const dom::StyleSheet &sheet, dom::NodeId node) {
    std::vector<PropertyTrace> traces;
    traces.reserve(dom::propertyCount);
    for (std::size_t p = 0; p < dom::propertyCount; ++p) {
        const auto property = static_cast<dom::Property>(p);
        const auto &source = resolver.provenance(node, property);
        const std::uint64_t packed = source.specificity.packed;
        PropertyTrace trace{property,
                            Origin::initial,
                            source.rule,
                            0,
                            static_cast<std::uint32_t>((packed >> 53) & 0x3FF),
                            static_cast<std::uint32_t>((packed >> 41) & 0xFFF),
                            static_cast<std::uint32_t>((packed >> 31) & 0x3FF),
                            source.inheritedFrom};
        if (source.inheritedFrom != dom::noIndex) {
            trace.origin = Origin::inherited;
        } else if (source.rule == dom::inlineRule) {
            trace.origin = Origin::inlineStyle;
        } else if (source.rule >= 0 && static_cast<std::size_t>(source.rule) < sheet.rules.size()) {
            trace.origin = Origin::rule;
            trace.line = sheet.rules[static_cast<std::size_t>(source.rule)].line;
        }
        traces.push_back(trace);
    }
    return traces;
}

std::string describeNode(const dom::DomDocument &document, const dom::StyleResolver &resolver,
                         const dom::StyleSheet &sheet, dom::NodeId node) {
    std::string report(document.typeName(document.node(node)));
    for (const auto &trace : traceNode(resolver, sheet, node)) {
        if (trace.origin == Origin::initial) {
            continue;
        }
        report += '\n';
        report += dom::propertyNames[static_cast<std::size_t>(trace.property)];
        switch (trace.origin) {
        case Origin::rule:
            report += ": rule " + std::to_string(trace.rule) + " line " +
                      std::to_string(trace.line) + " specificity " + std::to_string(trace.ids) +
                      "," + std::to_string(trace.classes) + "," + std::to_string(trace.types);
            break;
        case Origin::inlineStyle:
            report += ": inline";
            break;
        case Origin::inherited:
            report += ": inherited from node " + std::to_string(trace.inheritedFrom);
            break;
        case Origin::initial:
            break;
        }
    }
    return report;
}
}
