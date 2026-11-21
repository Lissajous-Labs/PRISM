#pragma once

#include "hal/DamageRegion.h"
#include "hal/HalTypes.h"
#include "math/Affine2D.h"
#include "math/Bitset64.h"
#include "render/DrawList.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace prism::scene {
using NodeId = std::uint32_t;
inline constexpr NodeId rootNode = 0;
inline constexpr NodeId invalidNode = std::numeric_limits<NodeId>::max();
inline constexpr std::size_t maxNodes = std::size_t{1} << 16;

enum class PaintKind : std::uint8_t { none, roundedRect, custom };

struct Paint {
    PaintKind kind = PaintKind::none;
    render::RectF rect{};
    math::CornerRadii radii{};
    std::uint32_t color = 0;
    std::optional<render::BorderStyle> border;
    void (*draw)(render::DrawList &, const void *) = nullptr;
    const void *context = nullptr;

    [[nodiscard]] static Paint box(render::RectF rect, math::CornerRadii radii,
                                   std::uint32_t color) {
        Paint paint;
        paint.kind = PaintKind::roundedRect;
        paint.rect = rect;
        paint.radii = radii;
        paint.color = color;
        return paint;
    }
};

class SceneGraph {
  public:
    explicit SceneGraph(hal::Extent viewport);

    [[nodiscard]] NodeId create(NodeId parent);
    void destroy(NodeId id);
    void setTransform(NodeId id, const math::Affine2D &local);
    void setClip(NodeId id, std::optional<render::RectF> clip, math::CornerRadii radii = {});
    void setPaint(NodeId id, const Paint &paint);
    void setVisible(NodeId id, bool visible);
    void setViewport(hal::Extent viewport);

    std::size_t update(hal::DamageRegion &damage);
    void record(render::DrawList &list, const hal::DamageRegion *cull = nullptr) const;

    [[nodiscard]] hal::Rect bounds(NodeId id) const noexcept { return nodes_[id].bounds; }
    [[nodiscard]] const math::Affine2D &world(NodeId id) const noexcept { return nodes_[id].world; }
    [[nodiscard]] std::size_t liveCount() const noexcept { return live_; }

  private:
    enum Dirty : std::uint8_t { paintDirty = 1, geometryDirty = 2 };

    struct Node {
        NodeId parent = invalidNode;
        NodeId firstChild = invalidNode;
        NodeId lastChild = invalidNode;
        NodeId previousSibling = invalidNode;
        NodeId nextSibling = invalidNode;
        math::Affine2D local{};
        math::Affine2D world{};
        render::RectF clip{};
        math::CornerRadii clipRadii{};
        hal::Rect clipBounds{};
        hal::Rect bounds{};
        Paint paint{};
        std::uint32_t epoch = 0;
        std::uint8_t dirty = 0;
        bool hasClip = false;
        bool visible = true;
        bool shown = true;
        bool alive = false;
    };

    std::vector<Node> nodes_;
    std::vector<NodeId> free_;
    math::Bitset64<maxNodes> pending_;
    hal::DamageRegion removed_;
    hal::Extent viewport_;
    std::uint32_t epoch_ = 1;
    std::size_t visits_ = 0;
    std::size_t live_ = 0;

    void mark(NodeId id, std::uint8_t flags);
    void compute(Node &node);
    void refresh(NodeId id, hal::DamageRegion &damage);
    void propagate(NodeId id, hal::DamageRegion &damage);
    void damageSubtree(NodeId id, hal::DamageRegion &damage);
    void recordNode(NodeId id, render::DrawList &list, std::uint16_t transform, std::uint16_t clip,
                    const hal::Rect *cull) const;
};
}
