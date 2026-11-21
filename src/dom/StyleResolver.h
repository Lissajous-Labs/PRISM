#pragma once

#include "dom/ComputedStyle.h"
#include "dom/DomDocument.h"
#include "dom/StyleSheet.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace prism::dom {
inline constexpr std::int32_t defaultRule = -1;
inline constexpr std::int32_t inlineRule = -2;

struct Provenance {
    std::int32_t rule = defaultRule;
    Specificity specificity{};
    std::uint32_t inheritedFrom = noIndex;
};

struct ResolveStats {
    std::size_t visited = 0;
    std::size_t ancestorChecks = 0;
    std::size_t bloomRejected = 0;
    std::size_t bloomFalsePositives = 0;
};

class StyleResolver {
  public:
    explicit StyleResolver(StyleSheet &sheet) : sheet_(&sheet) {}

    void setViewport(float width, float height) noexcept {
        width_ = width;
        height_ = height;
    }
    void setParameter(std::string_view name, double value);
    void setProvenance(bool enabled) noexcept { provenanceEnabled_ = enabled; }

    void resolve(const DomDocument &document);
    void restyle(const DomDocument &document, NodeId subtree);

    [[nodiscard]] const ComputedStyle &style(NodeId id) const noexcept { return styles_[id.index]; }
    [[nodiscard]] std::uint8_t lastDiff(NodeId id) const noexcept { return diffs_[id.index]; }
    [[nodiscard]] const Provenance &provenance(NodeId id, Property property) const noexcept {
        return provenance_[id.index * propertyCount + static_cast<std::size_t>(property)];
    }
    [[nodiscard]] const ResolveStats &stats() const noexcept { return stats_; }

  private:
    struct NodeTokens {
        std::uint64_t type = 0;
        std::uint64_t element = 0;
        SmallVector<std::uint64_t, maxClasses> classes;
    };

    struct Winner {
        Specificity specificity{};
        const StyleValue *value = nullptr;
        std::int32_t rule = defaultRule;
    };

    StyleSheet *sheet_;
    float width_ = 0;
    float height_ = 0;
    bool provenanceEnabled_ = true;
    std::vector<std::pair<std::string, double>> parameters_;
    std::vector<ComputedStyle> styles_;
    std::vector<std::uint8_t> diffs_;
    std::vector<std::uint32_t> generations_;
    std::vector<Bloom256> blooms_;
    std::vector<NodeTokens> tokens_;
    std::vector<Provenance> provenance_;
    std::unordered_map<std::uint64_t, std::vector<Declaration>> inlineCache_;
    ResolveStats stats_;

    void ensureCapacity(const DomDocument &document);
    void resolveNode(const DomDocument &document, std::uint32_t index);
    [[nodiscard]] bool matchCompound(const DomDocument &document, const Compound &compound,
                                     std::uint32_t index) const;
    [[nodiscard]] bool matchFrom(const DomDocument &document, const StyleRule &rule,
                                 std::size_t compound, std::uint32_t index) const;
    [[nodiscard]] const std::vector<Declaration> &inlineDeclarations(std::string_view source);
    [[nodiscard]] const StyleValue *resolveValue(Property property, const StyleValue *value) const;
    void apply(ComputedStyle &style, Property property, const StyleValue &value) const;
    void inherit(ComputedStyle &style, const ComputedStyle &parent, Property property) const;
};
}
