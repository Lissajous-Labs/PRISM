#include "dom/DomCodec.h"

#include <string>
#include <vector>

namespace prism::dom {
namespace {
struct ChildKey {
    std::string_view type;
    std::string_view element;
    std::size_t ordinal;

    friend bool operator==(const ChildKey &, const ChildKey &) = default;
};

std::vector<ChildKey> childKeys(const DomDocument &document, NodeId id,
                                std::vector<NodeId> &children) {
    children.clear();
    document.forEachChild(id, [&](NodeId child) { children.push_back(child); });
    std::vector<ChildKey> keys;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const DomNode &node = document.node(children[i]);
        std::size_t ordinal = 0;
        if (node.element == noString) {
            for (std::size_t j = 0; j < i; ++j) {
                const DomNode &earlier = document.node(children[j]);
                ordinal += earlier.element == noString && earlier.type == node.type ? 1 : 0;
            }
        }
        keys.push_back({document.typeName(node), document.text(node.element), ordinal});
    }
    return keys;
}

DomNode translated(DomDocument &live, const DomNode &base, const DomDocument &next,
                   const DomNode &source) {
    DomNode result = base;
    StringPool &pool = live.strings();
    result.element = pool.intern(next.text(source.element));
    result.pseudo = source.pseudo;
    result.classes.clear();
    for (const StringId name : source.classes) {
        static_cast<void>(result.classes.push_back(pool.intern(next.text(name))));
    }
    result.attributes.clear();
    for (const Attribute &attribute : source.attributes) {
        static_cast<void>(result.attributes.push_back(
            {pool.intern(next.text(attribute.name)), pool.intern(next.text(attribute.value))}));
    }
    result.bindings.clear();
    for (const Binding &binding : source.bindings) {
        static_cast<void>(
            result.bindings.push_back({binding.kind, pool.intern(next.text(binding.source)),
                                       pool.intern(next.text(binding.path))}));
    }
    return result;
}

std::size_t copySubtree(DomDocument &live, NodeId parent, const DomDocument &next, NodeId source) {
    const DomNode &from = next.node(source);
    const NodeId id = live.create(next.typeName(from), parent);
    if (!id.valid()) {
        return 0;
    }
    live.node(id) = translated(live, live.node(id), next, from);
    std::size_t count = 1;
    next.forEachChild(source, [&](NodeId child) { count += copySubtree(live, id, next, child); });
    return count;
}

std::size_t subtreeSize(const DomDocument &document, NodeId id) {
    std::size_t count = 1;
    document.forEachChild(id, [&](NodeId child) { count += subtreeSize(document, child); });
    return count;
}

void reconcileNode(DomDocument &live, NodeId liveId, const DomDocument &next, NodeId nextId,
                   ReconcileStats &stats) {
    const DomNode updated = translated(live, live.node(liveId), next, next.node(nextId));
    DomNode &current = live.node(liveId);
    if (updated.element != current.element || updated.pseudo != current.pseudo ||
        !(updated.classes == current.classes) || !(updated.attributes == current.attributes) ||
        !(updated.bindings == current.bindings)) {
        current = updated;
        ++stats.updated;
    } else {
        ++stats.kept;
    }
    std::vector<NodeId> liveChildren;
    std::vector<NodeId> nextChildren;
    const auto liveKeys = childKeys(live, liveId, liveChildren);
    const auto nextKeys = childKeys(next, nextId, nextChildren);
    std::vector<bool> used(liveChildren.size(), false);
    for (std::size_t n = 0; n < nextChildren.size(); ++n) {
        std::size_t match = liveChildren.size();
        for (std::size_t l = 0; l < liveChildren.size(); ++l) {
            if (!used[l] && liveKeys[l] == nextKeys[n]) {
                match = l;
                break;
            }
        }
        if (match == liveChildren.size()) {
            stats.created += copySubtree(live, liveId, next, nextChildren[n]);
            continue;
        }
        used[match] = true;
        live.moveToEnd(liveChildren[match]);
        reconcileNode(live, liveChildren[match], next, nextChildren[n], stats);
    }
    for (std::size_t l = 0; l < liveChildren.size(); ++l) {
        if (!used[l]) {
            stats.removed += subtreeSize(live, liveChildren[l]);
            live.remove(liveChildren[l]);
        }
    }
}
}

ReconcileStats reconcile(DomDocument &live, const DomDocument &next) {
    ReconcileStats stats;
    const NodeId liveRoot = live.root();
    const NodeId nextRoot = next.root();
    if (liveRoot.valid() && (!nextRoot.valid() || live.typeName(live.node(liveRoot)) !=
                                                      next.typeName(next.node(nextRoot)))) {
        stats.removed += subtreeSize(live, liveRoot);
        live.remove(liveRoot);
    }
    if (!nextRoot.valid()) {
        return stats;
    }
    if (!live.root().valid()) {
        stats.created += copySubtree(live, {}, next, nextRoot);
        return stats;
    }
    reconcileNode(live, live.root(), next, nextRoot, stats);
    return stats;
}
}
