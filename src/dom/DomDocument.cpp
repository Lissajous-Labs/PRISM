#include "dom/DomDocument.h"

namespace prism::dom {
StringId StringPool::intern(std::string_view text) {
    if (const auto found = lookup_.find(text); found != lookup_.end()) {
        return found->second;
    }
    const auto id = static_cast<StringId>(storage_.size());
    storage_.emplace_back(text);
    lookup_.emplace(storage_.back(), id);
    return id;
}

std::optional<StringId> StringPool::find(std::string_view text) const {
    if (const auto found = lookup_.find(text); found != lookup_.end()) {
        return found->second;
    }
    return std::nullopt;
}

std::optional<TypeId> DomDocument::typeFor(std::string_view type) {
    const StringId name = strings_.intern(type);
    if (const auto found = typeLookup_.find(name); found != typeLookup_.end()) {
        return found->second;
    }
    if (types_.size() > std::numeric_limits<TypeId>::max()) {
        return std::nullopt;
    }
    const auto id = static_cast<TypeId>(types_.size());
    types_.push_back(name);
    typeLookup_.emplace(name, id);
    return id;
}

void DomDocument::link(std::uint32_t index, std::uint32_t parent) {
    DomNode &node = nodes_[index];
    DomNode &owner = nodes_[parent];
    node.parent = parent;
    node.nextSibling = noIndex;
    node.previousSibling = owner.lastChild;
    if (owner.lastChild != noIndex) {
        nodes_[owner.lastChild].nextSibling = index;
    } else {
        owner.firstChild = index;
    }
    owner.lastChild = index;
}

void DomDocument::unlink(std::uint32_t index) {
    DomNode &node = nodes_[index];
    if (node.parent == noIndex) {
        return;
    }
    DomNode &owner = nodes_[node.parent];
    if (node.previousSibling != noIndex) {
        nodes_[node.previousSibling].nextSibling = node.nextSibling;
    } else {
        owner.firstChild = node.nextSibling;
    }
    if (node.nextSibling != noIndex) {
        nodes_[node.nextSibling].previousSibling = node.previousSibling;
    } else {
        owner.lastChild = node.previousSibling;
    }
    node.previousSibling = noIndex;
    node.nextSibling = noIndex;
}

NodeId DomDocument::create(std::string_view type, NodeId parent) {
    if (parent.valid() ? !contains(parent) : root_ != noIndex) {
        return {};
    }
    const auto typeId = typeFor(type);
    if (!typeId) {
        return {};
    }
    std::uint32_t index;
    if (!free_.empty()) {
        index = free_.back();
        free_.pop_back();
    } else {
        index = static_cast<std::uint32_t>(nodes_.size());
        nodes_.emplace_back();
    }
    const std::uint32_t generation = nodes_[index].generation + 1;
    nodes_[index] = DomNode{};
    nodes_[index].generation = generation;
    nodes_[index].type = *typeId;
    nodes_[index].alive = true;
    if (parent.valid()) {
        link(index, parent.index);
    } else {
        root_ = index;
    }
    ++live_;
    return {index, generation};
}

void DomDocument::release(std::uint32_t index) {
    for (std::uint32_t child = nodes_[index].firstChild; child != noIndex;) {
        const std::uint32_t next = nodes_[child].nextSibling;
        release(child);
        child = next;
    }
    nodes_[index].alive = false;
    free_.push_back(index);
    --live_;
}

void DomDocument::remove(NodeId id) {
    if (!contains(id)) {
        return;
    }
    unlink(id.index);
    if (id.index == root_) {
        root_ = noIndex;
    }
    release(id.index);
}

void DomDocument::moveToEnd(NodeId id) {
    const std::uint32_t parent = nodes_[id.index].parent;
    if (parent == noIndex || nodes_[parent].lastChild == id.index) {
        return;
    }
    unlink(id.index);
    link(id.index, parent);
}

bool DomDocument::setElementId(NodeId id, std::string_view element) {
    nodes_[id.index].element = strings_.intern(element);
    return true;
}

bool DomDocument::addClass(NodeId id, std::string_view name) {
    const StringId value = strings_.intern(name);
    for (const StringId existing : nodes_[id.index].classes) {
        if (existing == value) {
            return true;
        }
    }
    return nodes_[id.index].classes.push_back(value);
}

bool DomDocument::setAttribute(NodeId id, std::string_view name, std::string_view value) {
    const StringId key = strings_.intern(name);
    const StringId text = strings_.intern(value);
    for (Attribute &attribute : nodes_[id.index].attributes) {
        if (attribute.name == key) {
            attribute.value = text;
            return true;
        }
    }
    return nodes_[id.index].attributes.push_back({key, text});
}

bool DomDocument::addBinding(NodeId id, BindingKind kind, std::string_view source,
                             std::string_view path) {
    return nodes_[id.index].bindings.push_back(
        {kind, strings_.intern(source), strings_.intern(path)});
}

std::optional<std::string_view> DomDocument::attribute(NodeId id, std::string_view name) const {
    const auto key = strings_.find(name);
    if (!key) {
        return std::nullopt;
    }
    for (const Attribute &attribute : nodes_[id.index].attributes) {
        if (attribute.name == *key) {
            return strings_.view(attribute.value);
        }
    }
    return std::nullopt;
}

void DomDocument::forEachChild(NodeId id, FunctionRef<void(NodeId)> visitor) const {
    for (std::uint32_t child = nodes_[id.index].firstChild; child != noIndex;
         child = nodes_[child].nextSibling) {
        visitor(handle(child));
    }
}

bool DomDocument::equivalentNode(std::uint32_t mine, const DomDocument &other,
                                 std::uint32_t theirs) const {
    const DomNode &a = nodes_[mine];
    const DomNode &b = other.nodes_[theirs];
    const auto same = [&](StringId x, StringId y) { return text(x) == other.text(y); };
    if (typeName(a) != other.typeName(b) || !same(a.element, b.element) || a.pseudo != b.pseudo ||
        a.classes.size() != b.classes.size() || a.attributes.size() != b.attributes.size() ||
        a.bindings.size() != b.bindings.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.classes.size(); ++i) {
        if (!same(a.classes[i], b.classes[i])) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.attributes.size(); ++i) {
        if (!same(a.attributes[i].name, b.attributes[i].name) ||
            !same(a.attributes[i].value, b.attributes[i].value)) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.bindings.size(); ++i) {
        if (a.bindings[i].kind != b.bindings[i].kind ||
            !same(a.bindings[i].source, b.bindings[i].source) ||
            !same(a.bindings[i].path, b.bindings[i].path)) {
            return false;
        }
    }
    std::uint32_t x = a.firstChild;
    std::uint32_t y = b.firstChild;
    for (; x != noIndex && y != noIndex;
         x = nodes_[x].nextSibling, y = other.nodes_[y].nextSibling) {
        if (!equivalentNode(x, other, y)) {
            return false;
        }
    }
    return x == noIndex && y == noIndex;
}

bool DomDocument::equivalent(const DomDocument &other) const {
    if (root_ == noIndex || other.root_ == noIndex) {
        return root_ == other.root_;
    }
    return live_ == other.live_ && equivalentNode(root_, other, other.root_);
}
}
