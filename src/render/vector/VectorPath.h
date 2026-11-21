#pragma once

#include "math/Affine2D.h"
#include "math/Vector2.h"
#include "render/vector/PathTypes.h"

#include <cstdint>
#include <span>
#include <vector>

namespace prism::render {
enum class PathVerb : std::uint8_t { move, line, quad, cubic, arc, close };

struct Polyline {
    std::vector<math::Vector2> points;
    bool closed = false;
};

class VectorPath {
  public:
    void clear() noexcept;
    void moveTo(math::Vector2 point);
    void lineTo(math::Vector2 point);
    void quadTo(math::Vector2 control, math::Vector2 point);
    void cubicTo(math::Vector2 first, math::Vector2 second, math::Vector2 point);
    void arcTo(math::Vector2 center, double radius, double startAngle, double sweep);
    void close();

    [[nodiscard]] std::span<const PathVerb> verbs() const noexcept { return verbs_; }
    [[nodiscard]] std::span<const math::Vector2> points() const noexcept { return points_; }

  private:
    std::vector<PathVerb> verbs_;
    std::vector<math::Vector2> points_;
    math::Vector2 current_{0, 0};
};

void flattenPolylines(const VectorPath &path, const math::Affine2D &transform, double tolerance,
                      std::vector<Polyline> &output);
void appendFill(std::span<const Polyline> polylines, std::vector<LineSegment> &output);
}
