#include "scene/SceneGraph.h"

#include <algorithm>
#include <cmath>

namespace prism::scene {
namespace {
constexpr hal::Rect emptyRect{0, 0, 0, 0};

hal::Rect intersect(const hal::Rect &a, const hal::Rect &b) noexcept {
    const hal::Rect result{std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1),
                           std::min(a.y1, b.y1)};
    return result.empty() ? emptyRect : result;
}

bool overlaps(const hal::Rect &a, const hal::Rect &b) noexcept {
    return !a.empty() && !b.empty() && a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

hal::Rect transformedBounds(const math::Affine2D &transform, render::RectF rect, double margin,
                            double physicalMargin) noexcept {
    const double x0 = rect.x0 - margin;
    const double y0 = rect.y0 - margin;
    const double x1 = rect.x1 + margin;
    const double y1 = rect.y1 + margin;
    double minX = INFINITY;
    double minY = INFINITY;
    double maxX = -INFINITY;
    double maxY = -INFINITY;
    for (const math::Point2 corner :
         {math::Point2{x0, y0}, math::Point2{x1, y0}, math::Point2{x0, y1}, math::Point2{x1, y1}}) {
        const math::Point2 p = transform.apply(corner);
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }
    constexpr double limit = 1 << 28;
    const auto snap = [limit](double v) {
        return static_cast<std::int32_t>(std::clamp(v, -limit, limit));
    };
    return {snap(std::floor(minX - physicalMargin)), snap(std::floor(minY - physicalMargin)),
            snap(std::ceil(maxX + physicalMargin)), snap(std::ceil(maxY + physicalMargin))};
}

bool isIdentity(const math::Affine2D &t) noexcept {
    return t.a == 1 && t.b == 0 && t.c == 0 && t.d == 1 && t.tx == 0 && t.ty == 0;
}

bool rounded(const math::CornerRadii &r) noexcept {
    return r.topLeft > 0 || r.topRight > 0 || r.bottomLeft > 0 || r.bottomRight > 0;
}
}

SceneGraph::SceneGraph(hal::Extent viewport) : viewport_(viewport) {
    nodes_.emplace_back();
    nodes_[rootNode].alive = true;
    live_ = 1;
    mark(rootNode, geometryDirty);
}

void SceneGraph::mark(NodeId id, std::uint8_t flags) {
    nodes_[id].dirty |= flags;
    pending_.set(id);
}

NodeId SceneGraph::create(NodeId parent) {
    NodeId id;
    if (!free_.empty()) {
        id = free_.back();
        free_.pop_back();
        nodes_[id] = Node{};
    } else if (nodes_.size() < maxNodes) {
        id = static_cast<NodeId>(nodes_.size());
        nodes_.emplace_back();
    } else {
        return invalidNode;
    }
    Node &node = nodes_[id];
    Node &owner = nodes_[parent];
    node.alive = true;
    node.parent = parent;
    node.previousSibling = owner.lastChild;
    if (owner.lastChild != invalidNode) {
        nodes_[owner.lastChild].nextSibling = id;
    } else {
        owner.firstChild = id;
    }
    owner.lastChild = id;
    ++live_;
    mark(id, geometryDirty);
    return id;
}

void SceneGraph::damageSubtree(NodeId id, hal::DamageRegion &damage) {
    Node &node = nodes_[id];
    damage.add(node.bounds);
    for (NodeId child = node.firstChild; child != invalidNode;) {
        const NodeId next = nodes_[child].nextSibling;
        damageSubtree(child, damage);
        child = next;
    }
    node.alive = false;
    free_.push_back(id);
    --live_;
}

void SceneGraph::destroy(NodeId id) {
    if (id == rootNode || !nodes_[id].alive) {
        return;
    }
    Node &node = nodes_[id];
    Node &owner = nodes_[node.parent];
    if (node.previousSibling != invalidNode) {
        nodes_[node.previousSibling].nextSibling = node.nextSibling;
    } else {
        owner.firstChild = node.nextSibling;
    }
    if (node.nextSibling != invalidNode) {
        nodes_[node.nextSibling].previousSibling = node.previousSibling;
    } else {
        owner.lastChild = node.previousSibling;
    }
    damageSubtree(id, removed_);
}

void SceneGraph::setTransform(NodeId id, const math::Affine2D &local) {
    nodes_[id].local = local;
    mark(id, geometryDirty);
}

void SceneGraph::setClip(NodeId id, std::optional<render::RectF> clip, math::CornerRadii radii) {
    Node &node = nodes_[id];
    node.hasClip = clip.has_value();
    node.clip = clip.value_or(render::RectF{});
    node.clipRadii = radii;
    mark(id, geometryDirty);
}

void SceneGraph::setPaint(NodeId id, const Paint &paint) {
    nodes_[id].paint = paint;
    mark(id, paintDirty);
}

void SceneGraph::setVisible(NodeId id, bool visible) {
    nodes_[id].visible = visible;
    mark(id, geometryDirty);
}

void SceneGraph::setViewport(hal::Extent viewport) {
    viewport_ = viewport;
    mark(rootNode, geometryDirty);
}

void SceneGraph::compute(Node &node) {
    const hal::Rect screen{0, 0, static_cast<std::int32_t>(viewport_.width),
                           static_cast<std::int32_t>(viewport_.height)};
    if (node.parent == invalidNode) {
        node.world = node.local;
        node.shown = node.visible;
        node.clipBounds = screen;
    } else {
        const Node &parent = nodes_[node.parent];
        node.world = parent.world * node.local;
        node.shown = parent.shown && node.visible;
        node.clipBounds = parent.clipBounds;
    }
    if (node.hasClip) {
        node.clipBounds =
            intersect(node.clipBounds, transformedBounds(node.world, node.clip, 0, 1));
    }
    node.bounds = emptyRect;
    if (node.shown && node.paint.kind != PaintKind::none) {
        node.bounds =
            intersect(node.clipBounds, transformedBounds(node.world, node.paint.rect, 1.5, 1));
    }
}

void SceneGraph::propagate(NodeId id, hal::DamageRegion &damage) {
    Node &node = nodes_[id];
    damage.add(node.bounds);
    compute(node);
    damage.add(node.bounds);
    node.epoch = epoch_;
    node.dirty = 0;
    ++visits_;
    for (NodeId child = node.firstChild; child != invalidNode; child = nodes_[child].nextSibling) {
        propagate(child, damage);
    }
}

void SceneGraph::refresh(NodeId id, hal::DamageRegion &damage) {
    Node &node = nodes_[id];
    if (!node.alive || node.epoch == epoch_) {
        return;
    }
    if (node.parent != invalidNode && pending_.test(node.parent)) {
        refresh(node.parent, damage);
        if (node.epoch == epoch_) {
            return;
        }
    }
    if ((node.dirty & geometryDirty) != 0) {
        propagate(id, damage);
        return;
    }
    damage.add(node.bounds);
    compute(node);
    damage.add(node.bounds);
    node.epoch = epoch_;
    node.dirty = 0;
    ++visits_;
}

std::size_t SceneGraph::update(hal::DamageRegion &damage) {
    damage.clear();
    damage.apply(hal::RegionOp::unite, removed_);
    removed_.clear();
    epoch_ = epoch_ == std::numeric_limits<std::uint32_t>::max() ? 1 : epoch_ + 1;
    visits_ = 0;
    pending_.forEach([&](std::size_t id) { refresh(static_cast<NodeId>(id), damage); });
    pending_.clear();
    damage.clip({0, 0, static_cast<std::int32_t>(viewport_.width),
                 static_cast<std::int32_t>(viewport_.height)});
    return visits_;
}

void SceneGraph::record(render::DrawList &list, const hal::DamageRegion *cull) const {
    const hal::Rect area = cull != nullptr ? cull->bounds() : emptyRect;
    recordNode(rootNode, list, render::identityTransform, render::noClip,
               cull != nullptr ? &area : nullptr);
    list.setTransform(render::identityTransform);
    list.setClip(render::noClip);
}

void SceneGraph::recordNode(NodeId id, render::DrawList &list, std::uint16_t transform,
                            std::uint16_t clip, const hal::Rect *cull) const {
    const Node &node = nodes_[id];
    if (!node.shown || (cull != nullptr && !overlaps(node.clipBounds, *cull))) {
        return;
    }
    if (!isIdentity(node.local)) {
        const auto added = list.addTransform(node.world);
        if (!added) {
            return;
        }
        transform = *added;
    }
    if (node.hasClip) {
        list.setTransform(transform);
        list.setClip(clip);
        clip = list.addClip(rounded(node.clipRadii) ? render::ClipKind::roundedRect
                                                    : render::ClipKind::rect,
                            node.clip, node.clipRadii);
    }
    if (node.paint.kind != PaintKind::none && (cull == nullptr || overlaps(node.bounds, *cull))) {
        list.setTransform(transform);
        list.setClip(clip);
        if (node.paint.kind == PaintKind::roundedRect) {
            list.roundedRect(node.paint.rect, node.paint.radii, node.paint.color,
                             node.paint.border);
        } else if (node.paint.draw != nullptr) {
            node.paint.draw(list, node.paint.context);
        }
    }
    for (NodeId child = node.firstChild; child != invalidNode; child = nodes_[child].nextSibling) {
        recordNode(child, list, transform, clip, cull);
    }
}
}
