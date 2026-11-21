#include "dom/StyleResolver.h"

#include <array>

namespace prism::dom {
void StyleResolver::setParameter(std::string_view name, double value) {
    for (auto &[key, current] : parameters_) {
        if (key == name) {
            current = value;
            return;
        }
    }
    parameters_.emplace_back(std::string(name), value);
}

void StyleResolver::ensureCapacity(const DomDocument &document) {
    const std::size_t size = document.capacity();
    if (styles_.size() < size) {
        styles_.resize(size);
        diffs_.resize(size, diff::structure);
        generations_.resize(size, 0);
        blooms_.resize(size);
        tokens_.resize(size);
        provenance_.resize(size * propertyCount);
    }
}

void StyleResolver::resolve(const DomDocument &document) {
    ensureCapacity(document);
    stats_ = {};
    if (document.root().valid()) {
        resolveNode(document, document.root().index);
    }
}

void StyleResolver::restyle(const DomDocument &document, NodeId subtree) {
    ensureCapacity(document);
    stats_ = {};
    if (document.contains(subtree)) {
        resolveNode(document, subtree.index);
    }
}

bool StyleResolver::matchCompound(const DomDocument &document, const Compound &compound,
                                  std::uint32_t index) const {
    const DomNode &node = document.node(document.handle(index));
    const NodeTokens &tokens = tokens_[index];
    if ((compound.type != 0 && compound.type != tokens.type) ||
        (compound.element != 0 && compound.element != tokens.element) ||
        (node.pseudo & compound.pseudo) != compound.pseudo ||
        (compound.root && node.parent != noIndex)) {
        return false;
    }
    for (const std::uint64_t name : compound.classes) {
        bool found = false;
        for (const std::uint64_t own : tokens.classes) {
            found = found || own == name;
        }
        if (!found) {
            return false;
        }
    }
    for (const Predicate &predicate : compound.predicates) {
        const std::string &name = sheet_->strings[predicate.name];
        if (predicate.op == PredicateOp::exists || predicate.op == PredicateOp::attributeEquals) {
            const auto value = document.attribute(document.handle(index), name);
            if (!value || (predicate.op == PredicateOp::attributeEquals &&
                           *value != sheet_->strings[predicate.text])) {
                return false;
            }
            continue;
        }
        const double *parameter = nullptr;
        for (const auto &[key, value] : parameters_) {
            parameter = key == name ? &value : parameter;
        }
        if (parameter == nullptr) {
            return false;
        }
        const double x = *parameter;
        const double y = predicate.number;
        const bool holds = predicate.op == PredicateOp::eq   ? x == y
                           : predicate.op == PredicateOp::ne ? x != y
                           : predicate.op == PredicateOp::lt ? x < y
                           : predicate.op == PredicateOp::gt ? x > y
                           : predicate.op == PredicateOp::le ? x <= y
                                                             : x >= y;
        if (!holds) {
            return false;
        }
    }
    return true;
}

bool StyleResolver::matchFrom(const DomDocument &document, const StyleRule &rule,
                              std::size_t compound, std::uint32_t index) const {
    if (!matchCompound(document, rule.compounds[compound], index)) {
        return false;
    }
    if (compound + 1 == rule.compounds.size()) {
        return true;
    }
    std::uint32_t ancestor = document.node(document.handle(index)).parent;
    if (rule.compounds[compound].toLeft == Combinator::child) {
        return ancestor != noIndex && matchFrom(document, rule, compound + 1, ancestor);
    }
    for (; ancestor != noIndex; ancestor = document.node(document.handle(ancestor)).parent) {
        if (matchFrom(document, rule, compound + 1, ancestor)) {
            return true;
        }
    }
    return false;
}

const std::vector<Declaration> &StyleResolver::inlineDeclarations(std::string_view source) {
    const std::uint64_t key = math::hash64(source);
    auto found = inlineCache_.find(key);
    if (found == inlineCache_.end()) {
        std::vector<Declaration> parsed;
        if (!parseDeclarations(source, *sheet_, parsed)) {
            parsed.clear();
        }
        found = inlineCache_.emplace(key, std::move(parsed)).first;
    }
    return found->second;
}

const StyleValue *StyleResolver::resolveValue(Property property, const StyleValue *value) const {
    if (value == nullptr) {
        return nullptr;
    }
    if (value->kind == ValueKind::token) {
        value = &sheet_->tokenValues[value->token];
    }
    const ValueKind expected = valueKindOf(property);
    if (value->kind == expected) {
        return value;
    }
    return nullptr;
}

void StyleResolver::apply(ComputedStyle &style, Property property, const StyleValue &value) const {
    ColorBlock &c = style.colors;
    TransformBlock &t = style.transform;
    LayoutBlock &l = style.layout;
    const Length length = value.length;
    switch (property) {
    case Property::color:
        c.color = value.color;
        break;
    case Property::background:
        c.background = value.color;
        break;
    case Property::borderColor:
        c.borderColor = value.color;
        break;
    case Property::opacity:
        c.opacity = value.number;
        break;
    case Property::borderRadius:
        c.borderRadius = length.value;
        break;
    case Property::translateX:
        t.translateX = length.value;
        break;
    case Property::translateY:
        t.translateY = length.value;
        break;
    case Property::scale:
        t.scale = value.number;
        break;
    case Property::rotate:
        t.rotate = value.number;
        break;
    case Property::width:
        l.width = length;
        break;
    case Property::height:
        l.height = length;
        break;
    case Property::minWidth:
        l.minWidth = length;
        break;
    case Property::minHeight:
        l.minHeight = length;
        break;
    case Property::maxWidth:
        l.maxWidth = length;
        break;
    case Property::maxHeight:
        l.maxHeight = length;
        break;
    case Property::margin:
        l.margin = length;
        break;
    case Property::padding:
        l.padding = length;
        break;
    case Property::gap:
        l.gap = length;
        break;
    case Property::flexBasis:
        l.flexBasis = length;
        break;
    case Property::left:
        l.left = length;
        break;
    case Property::top:
        l.top = length;
        break;
    case Property::right:
        l.right = length;
        break;
    case Property::bottom:
        l.bottom = length;
        break;
    case Property::borderWidth:
        l.borderWidth = length.value;
        break;
    case Property::flexGrow:
        l.flexGrow = value.number;
        break;
    case Property::flexShrink:
        l.flexShrink = value.number;
        break;
    case Property::fontSize:
        l.fontSize = length.unit == Unit::em ? l.fontSize * length.value : length.value;
        break;
    case Property::lineHeight:
        l.lineHeight = length.unit == Unit::em ? l.fontSize * length.value : length.value;
        break;
    case Property::order:
        l.order = static_cast<std::int32_t>(value.number);
        break;
    case Property::display:
        l.display = value.keyword;
        break;
    case Property::flexDirection:
        l.flexDirection = value.keyword;
        break;
    case Property::flexWrap:
        l.flexWrap = value.keyword;
        break;
    case Property::justifyContent:
        l.justifyContent = value.keyword;
        break;
    case Property::alignItems:
        l.alignItems = value.keyword;
        break;
    case Property::alignSelf:
        l.alignSelf = value.keyword;
        break;
    case Property::visibility:
        l.visibility = value.keyword;
        break;
    default:
        break;
    }
}

void StyleResolver::inherit(ComputedStyle &style, const ComputedStyle &parent,
                            Property property) const {
    switch (property) {
    case Property::color:
        style.colors.color = parent.colors.color;
        break;
    case Property::fontSize:
        style.layout.fontSize = parent.layout.fontSize;
        break;
    case Property::lineHeight:
        style.layout.lineHeight = parent.layout.lineHeight;
        break;
    case Property::visibility:
        style.layout.visibility = parent.layout.visibility;
        break;
    default:
        break;
    }
}

void StyleResolver::resolveNode(const DomDocument &document, std::uint32_t index) {
    ++stats_.visited;
    const NodeId id = document.handle(index);
    const DomNode &node = document.node(id);
    NodeTokens &tokens = tokens_[index];
    tokens = {};
    tokens.type = tokenHash(TokenKind::type, document.typeName(node));
    tokens.element =
        node.element != noString ? tokenHash(TokenKind::element, document.text(node.element)) : 0;
    for (const StringId name : node.classes) {
        static_cast<void>(
            tokens.classes.push_back(tokenHash(TokenKind::klass, document.text(name))));
    }
    const Bloom256 ancestors = node.parent != noIndex ? blooms_[node.parent] : Bloom256{};
    Bloom256 &bloom = blooms_[index];
    bloom = ancestors;
    bloom.add(tokens.type);
    if (tokens.element != 0) {
        bloom.add(tokens.element);
    }
    for (const std::uint64_t name : tokens.classes) {
        bloom.add(name);
    }

    std::array<Winner, propertyCount> winners{};
    std::array<const StyleValue *, 4> transitions{};
    std::size_t transitionCount = 0;
    Specificity transitionSpecificity{};
    const auto offer = [&](const Declaration &declaration, Specificity specificity,
                           std::int32_t rule) {
        if (declaration.property == Property::transition) {
            if (transitionCount == 0 || specificity > transitionSpecificity) {
                transitionCount = 0;
                transitionSpecificity = specificity;
            }
            if (specificity == transitionSpecificity && transitionCount < transitions.size()) {
                transitions[transitionCount++] = &declaration.value;
            }
            return;
        }
        Winner &winner = winners[static_cast<std::size_t>(declaration.property)];
        if (winner.value == nullptr || specificity >= winner.specificity) {
            winner = {specificity, &declaration.value, rule};
        }
    };
    for (std::size_t r = 0; r < sheet_->rules.size(); ++r) {
        const StyleRule &rule = sheet_->rules[r];
        if (!sheet_->media[rule.media].matches(width_, height_)) {
            continue;
        }
        if (rule.compounds.size() > 1) {
            ++stats_.ancestorChecks;
            if (!ancestors.covers(rule.ancestorMask)) {
                ++stats_.bloomRejected;
                continue;
            }
        }
        if (!matchFrom(document, rule, 0, index)) {
            if (rule.compounds.size() > 1 && matchCompound(document, rule.compounds[0], index)) {
                ++stats_.bloomFalsePositives;
            }
            continue;
        }
        for (std::uint32_t d = 0; d < rule.declarationCount; ++d) {
            offer(sheet_->declarations[rule.firstDeclaration + d], rule.specificity,
                  static_cast<std::int32_t>(r));
        }
    }
    if (const auto inlineStyle = document.attribute(id, "style")) {
        const auto &declarations = inlineDeclarations(*inlineStyle);
        for (std::size_t d = 0; d < declarations.size(); ++d) {
            offer(declarations[d], Specificity::of(true, 0, 0, 0, static_cast<std::uint32_t>(d)),
                  inlineRule);
        }
    }

    ComputedStyle style;
    const ComputedStyle *parent = node.parent != noIndex ? &styles_[node.parent] : nullptr;
    if (parent != nullptr) {
        style.layout.fontSize = parent->layout.fontSize;
    }
    for (std::size_t p = 0; p < propertyCount; ++p) {
        const auto property = static_cast<Property>(p);
        Provenance &origin = provenance_[index * propertyCount + p];
        const StyleValue *value = resolveValue(property, winners[p].value);
        if (value != nullptr) {
            apply(style, property, *value);
            if (provenanceEnabled_) {
                origin = {winners[p].rule, winners[p].specificity, noIndex};
            }
        } else if (parent != nullptr && inherited(property)) {
            inherit(style, *parent, property);
            if (provenanceEnabled_) {
                const Provenance &above = provenance_[node.parent * propertyCount + p];
                origin = above.inheritedFrom != noIndex
                             ? above
                             : Provenance{above.rule, above.specificity, node.parent};
            }
        } else if (provenanceEnabled_) {
            origin = {};
        }
    }
    for (std::size_t i = 0; i < transitionCount; ++i) {
        style.motion.properties[i] = transitions[i]->transitionProperty;
        style.motion.omega[i] = transitions[i]->number;
    }
    style.motion.count = static_cast<std::uint32_t>(transitionCount);

    const bool known = generations_[index] == node.generation;
    diffs_[index] = known ? styleDiff(styles_[index], style) : diff::structure;
    styles_[index] = style;
    generations_[index] = node.generation;
    for (std::uint32_t child = node.firstChild; child != noIndex;
         child = document.node(document.handle(child)).nextSibling) {
        resolveNode(document, child);
    }
}
}
