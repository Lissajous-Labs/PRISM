#include "render/vector/VectorPath.h"

#include "math/WangFlatten.h"

#include <cmath>

namespace prism::render {
namespace {
math::Vector2 apply(const math::Affine2D &transform, math::Vector2 point) noexcept {
    const auto mapped = transform.apply(math::Point2{point.x, point.y});
    return {mapped.x, mapped.y};
}

void finish(std::vector<Polyline> &output, Polyline &current, bool closed) {
    if (current.points.size() >= 2) {
        current.closed = closed;
        output.push_back(std::move(current));
    }
    current = Polyline{};
}
}

void VectorPath::clear() noexcept {
    verbs_.clear();
    points_.clear();
    current_ = {0, 0};
}

void VectorPath::moveTo(math::Vector2 point) {
    verbs_.push_back(PathVerb::move);
    points_.push_back(point);
    current_ = point;
}

void VectorPath::lineTo(math::Vector2 point) {
    verbs_.push_back(PathVerb::line);
    points_.push_back(point);
    current_ = point;
}

void VectorPath::quadTo(math::Vector2 control, math::Vector2 point) {
    verbs_.push_back(PathVerb::quad);
    points_.insert(points_.end(), {control, point});
    current_ = point;
}

void VectorPath::cubicTo(math::Vector2 first, math::Vector2 second, math::Vector2 point) {
    verbs_.push_back(PathVerb::cubic);
    points_.insert(points_.end(), {first, second, point});
    current_ = point;
}

void VectorPath::arcTo(math::Vector2 center, double radius, double startAngle, double sweep) {
    const math::Vector2 start =
        center + math::Vector2{std::cos(startAngle), std::sin(startAngle)} * radius;
    if (verbs_.empty() || verbs_.back() == PathVerb::close) {
        moveTo(start);
    } else {
        lineTo(start);
    }
    verbs_.push_back(PathVerb::arc);
    points_.insert(points_.end(), {center, {radius, 0}, {startAngle, sweep}});
    current_ =
        center + math::Vector2{std::cos(startAngle + sweep), std::sin(startAngle + sweep)} * radius;
}

void VectorPath::close() { verbs_.push_back(PathVerb::close); }

void flattenPolylines(const VectorPath &path, const math::Affine2D &transform, double tolerance,
                      std::vector<Polyline> &output) {
    Polyline current;
    std::size_t index = 0;
    math::Vector2 last{0, 0};
    for (const PathVerb verb : path.verbs()) {
        const auto points = path.points();
        switch (verb) {
        case PathVerb::move:
            finish(output, current, false);
            last = apply(transform, points[index++]);
            current.points.push_back(last);
            break;
        case PathVerb::line:
            last = apply(transform, points[index++]);
            current.points.push_back(last);
            break;
        case PathVerb::quad: {
            const math::Vector2 control = apply(transform, points[index]);
            const math::Vector2 end = apply(transform, points[index + 1]);
            index += 2;
            const auto count = math::quadraticSegments(last, control, end, tolerance);
            for (std::uint32_t i = 1; i <= count; ++i) {
                current.points.push_back(
                    math::quadraticPoint(last, control, end, static_cast<double>(i) / count));
            }
            last = end;
            break;
        }
        case PathVerb::cubic: {
            const math::Vector2 first = apply(transform, points[index]);
            const math::Vector2 second = apply(transform, points[index + 1]);
            const math::Vector2 end = apply(transform, points[index + 2]);
            index += 3;
            const auto count = math::cubicSegments(last, first, second, end, tolerance);
            for (std::uint32_t i = 1; i <= count; ++i) {
                current.points.push_back(
                    math::cubicPoint(last, first, second, end, static_cast<double>(i) / count));
            }
            last = end;
            break;
        }
        case PathVerb::arc: {
            const math::Vector2 center = points[index];
            const double radius = points[index + 1].x;
            const double start = points[index + 2].x;
            const double sweep = points[index + 2].y;
            index += 3;
            const double scale = transform.singularValues().maximum;
            const auto count = math::arcSegments(sweep, radius * scale, tolerance);
            for (std::uint32_t i = 1; i <= count; ++i) {
                const double angle = start + sweep * i / count;
                current.points.push_back(apply(
                    transform, center + math::Vector2{std::cos(angle), std::sin(angle)} * radius));
            }
            last = current.points.back();
            break;
        }
        case PathVerb::close:
            finish(output, current, true);
            current.points.push_back(last);
            break;
        }
    }
    finish(output, current, false);
}

void appendFill(std::span<const Polyline> polylines, std::vector<LineSegment> &output) {
    for (const Polyline &polyline : polylines) {
        const auto &points = polyline.points;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const math::Vector2 a = points[i];
            const math::Vector2 b = points[(i + 1) % points.size()];
            if (a == b) {
                continue;
            }
            output.push_back({static_cast<float>(a.x), static_cast<float>(a.y),
                              static_cast<float>(b.x), static_cast<float>(b.y)});
        }
    }
}
}
