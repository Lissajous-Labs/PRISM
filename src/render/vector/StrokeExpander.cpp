#include "render/vector/StrokeExpander.h"

#include "math/WangFlatten.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace prism::render {
namespace {
using math::Vector2;

Vector2 normalOf(Vector2 a, Vector2 b) noexcept {
    const Vector2 d = b - a;
    const double len = math::length(d);
    return len > 0 ? Vector2{-d.y / len, d.x / len} : Vector2{0, 0};
}

void appendArc(std::vector<Vector2> &points, Vector2 center, Vector2 from, Vector2 to,
               double radius, double tolerance, bool counterClockwise) {
    double start = std::atan2(from.y, from.x);
    double end = std::atan2(to.y, to.x);
    double sweep = end - start;
    if (counterClockwise) {
        while (sweep <= 0) {
            sweep += 2 * std::numbers::pi;
        }
    } else {
        while (sweep >= 0) {
            sweep -= 2 * std::numbers::pi;
        }
    }
    const auto count = math::arcSegments(sweep, radius, tolerance);
    for (std::uint32_t i = 1; i < count; ++i) {
        const double angle = start + sweep * i / count;
        points.push_back(center + Vector2{std::cos(angle), std::sin(angle)} * radius);
    }
}

std::vector<Vector2> cleaned(const Polyline &polyline) {
    std::vector<Vector2> points;
    for (const Vector2 point : polyline.points) {
        if (points.empty() || math::length(point - points.back()) > 1e-9) {
            points.push_back(point);
        }
    }
    if (polyline.closed && points.size() > 2 &&
        math::length(points.front() - points.back()) <= 1e-9) {
        points.pop_back();
    }
    return points;
}

struct SideBuilder {
    const StrokeStyle &style;
    double half;
    std::vector<Vector2> points;

    [[nodiscard]] Vector2 bisectorPoint(Vector2 pivot, Vector2 incoming, Vector2 outgoing,
                                        double side) const noexcept {
        const double cosine = std::clamp(math::dot(incoming, outgoing), -1.0, 1.0);
        const double cosecant = 1 / std::max(std::cos(std::acos(cosine) / 2), 1e-12);
        const Vector2 bisector = incoming + outgoing;
        const double len = math::length(bisector);
        return len > 1e-12 ? pivot + bisector * (half * side * cosecant / len) : pivot;
    }

    void join(Vector2 before, Vector2 pivot, Vector2 after, double side) {
        const Vector2 incoming = normalOf(before, pivot);
        const Vector2 outgoing = normalOf(pivot, after);
        const Vector2 previous = pivot + incoming * (half * side);
        const Vector2 next = pivot + outgoing * (half * side);
        const double turn =
            math::cross(Vector2{incoming.y, -incoming.x}, Vector2{outgoing.y, -outgoing.x});
        const bool outer = turn * side < 0;
        if (!outer) {
            const Vector2 corner = bisectorPoint(pivot, incoming, outgoing, side);
            const Vector2 offset = corner - pivot;
            const Vector2 inDirection{incoming.y, -incoming.x};
            const Vector2 outDirection{outgoing.y, -outgoing.x};
            const bool fits = -math::dot(offset, inDirection) <= math::length(pivot - before) &&
                              math::dot(offset, outDirection) <= math::length(after - pivot);
            if (fits && std::abs(turn) > 1e-9) {
                points.push_back(corner);
            } else {
                points.push_back(previous);
                points.push_back(pivot);
                points.push_back(next);
            }
            return;
        }
        points.push_back(previous);
        if (style.join == LineJoin::round) {
            appendArc(points, pivot, previous - pivot, next - pivot, half, style.tolerance,
                      side * turn > 0);
        } else if (style.join == LineJoin::miter) {
            const double cosine = std::clamp(math::dot(incoming, outgoing), -1.0, 1.0);
            const double cosecant = 1 / std::max(std::cos(std::acos(cosine) / 2), 1e-12);
            if (cosecant <= style.miterLimit) {
                points.push_back(bisectorPoint(pivot, incoming, outgoing, side));
            }
        }
        points.push_back(next);
    }

    void cap(Vector2 end, Vector2 normal, Vector2 direction) {
        const Vector2 left = end + normal * half;
        const Vector2 right = end - normal * half;
        if (style.cap == LineCap::square) {
            points.push_back(left + direction * half);
            points.push_back(right + direction * half);
        } else if (style.cap == LineCap::round) {
            points.push_back(left);
            appendArc(points, end, normal * half, normal * -half, half, style.tolerance, false);
            points.push_back(right);
        } else {
            points.push_back(left);
            points.push_back(right);
        }
    }
};

void appendSide(SideBuilder &builder, const std::vector<Vector2> &points) {
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        builder.join(points[i - 1], points[i], points[i + 1], 1);
    }
}

void strokeOpen(const std::vector<Vector2> &points, const StrokeStyle &style,
                std::vector<Polyline> &output) {
    const double half = style.width / 2;
    const std::vector<Vector2> reversed(points.rbegin(), points.rend());
    const Vector2 startNormal = normalOf(points[0], points[1]);
    const Vector2 endNormal = normalOf(points[points.size() - 2], points.back());
    SideBuilder builder{style, half, {}};
    builder.points.push_back(points[0] + startNormal * half);
    appendSide(builder, points);
    builder.cap(points.back(), endNormal, Vector2{endNormal.y, -endNormal.x});
    appendSide(builder, reversed);
    builder.cap(points[0], startNormal * -1, Vector2{-startNormal.y, startNormal.x});
    output.push_back({std::move(builder.points), true});
}

void strokeClosed(const std::vector<Vector2> &points, const StrokeStyle &style,
                  std::vector<Polyline> &output) {
    const double half = style.width / 2;
    const std::vector<Vector2> reversed(points.rbegin(), points.rend());
    for (const auto *ring : {&points, &reversed}) {
        const std::size_t count = ring->size();
        SideBuilder builder{style, half, {}};
        for (std::size_t i = 0; i < count; ++i) {
            const Vector2 previous = (*ring)[(i + count - 1) % count];
            const Vector2 next = (*ring)[(i + 1) % count];
            builder.join(previous, (*ring)[i], next, 1);
        }
        output.push_back({std::move(builder.points), true});
    }
}
}

void dashPolylines(std::span<const Polyline> input, std::span<const double> dashes, double offset,
                   std::vector<Polyline> &output) {
    double pattern = 0;
    for (const double dash : dashes) {
        pattern += std::max(dash, 0.0);
    }
    if (dashes.empty() || !(pattern > 0)) {
        output.insert(output.end(), input.begin(), input.end());
        return;
    }
    for (const Polyline &polyline : input) {
        std::vector<Vector2> points = polyline.points;
        if (polyline.closed && !points.empty()) {
            points.push_back(points.front());
        }
        std::size_t dashIndex = 0;
        double remaining = 0;
        double phase = std::fmod(std::fmod(offset, pattern) + pattern, pattern);
        while (true) {
            const double length = std::max(dashes[dashIndex], 0.0);
            if (phase < length) {
                remaining = length - phase;
                break;
            }
            phase -= length;
            dashIndex = (dashIndex + 1) % dashes.size();
        }
        bool on = dashIndex % 2 == 0;
        Polyline current;
        if (on && !points.empty()) {
            current.points.push_back(points.front());
        }
        for (std::size_t i = 0; i + 1 < points.size(); ++i) {
            Vector2 a = points[i];
            const Vector2 b = points[i + 1];
            double segmentLength = math::length(b - a);
            while (segmentLength > remaining) {
                const Vector2 cut = a + (b - a) * (remaining / segmentLength);
                if (on) {
                    current.points.push_back(cut);
                    if (current.points.size() >= 2) {
                        output.push_back(std::move(current));
                    }
                    current = Polyline{};
                } else {
                    current.points.push_back(cut);
                }
                on = !on;
                segmentLength -= remaining;
                a = cut;
                dashIndex = (dashIndex + 1) % dashes.size();
                remaining = std::max(dashes[dashIndex], 0.0);
            }
            remaining -= segmentLength;
            if (on) {
                current.points.push_back(b);
            }
        }
        if (on && current.points.size() >= 2) {
            output.push_back(std::move(current));
        }
    }
}

void strokeOutlines(std::span<const Polyline> input, const StrokeStyle &style,
                    std::vector<Polyline> &output) {
    std::vector<Polyline> dashed;
    dashPolylines(input, style.dashes, style.dashOffset, dashed);
    for (const Polyline &polyline : dashed) {
        const auto points = cleaned(polyline);
        if (points.size() < 2) {
            continue;
        }
        if (polyline.closed && points.size() >= 3) {
            strokeClosed(points, style, output);
        } else {
            strokeOpen(points, style, output);
        }
    }
}
}
