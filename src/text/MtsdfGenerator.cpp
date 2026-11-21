#include "text/MtsdfGenerator.h"

#include "math/SdfCurves.h"
#include "math/SdfShapes.h"
#include "render/vector/VectorPath.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace prism::text {
namespace {
using math::Vector2;

enum Channel : std::uint8_t { red = 1, green = 2, blue = 4, white = 7 };

struct Edge {
    Vector2 p0;
    Vector2 p1;
    Vector2 p2;
    bool quadratic;
    std::uint8_t color;

    [[nodiscard]] Vector2 point(double t) const noexcept {
        if (!quadratic) {
            return p0 + (p2 - p0) * t;
        }
        const double s = 1 - t;
        return p0 * (s * s) + p1 * (2 * s * t) + p2 * (t * t);
    }
    [[nodiscard]] Vector2 direction(double t) const noexcept {
        if (!quadratic) {
            return p2 - p0;
        }
        return (p1 - p0) * (2 * (1 - t)) + (p2 - p1) * (2 * t);
    }
};

struct EdgeDistance {
    double distance;
    double orthogonality;
    double signedPseudo;
};

double closestParameter(const Edge &edge, Vector2 p) {
    if (!edge.quadratic) {
        const Vector2 d = edge.p2 - edge.p0;
        const double len = math::dot(d, d);
        return len > 0 ? math::dot(p - edge.p0, d) / len : 0;
    }
    double best = 0;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (int i = 0; i <= 16; ++i) {
        const double t = i / 16.0;
        const double d = math::length(edge.point(t) - p);
        if (d < bestDistance) {
            bestDistance = d;
            best = t;
        }
    }
    for (int i = 0; i < 6; ++i) {
        const Vector2 offset = edge.point(best) - p;
        const Vector2 tangent = edge.direction(best);
        const Vector2 second = (edge.p0 - edge.p1 * 2 + edge.p2) * 2;
        const double slope = math::dot(tangent, tangent) + math::dot(offset, second);
        if (!(slope > 0)) {
            break;
        }
        best -= math::dot(offset, tangent) / slope;
    }
    return best;
}

EdgeDistance measure(const Edge &edge, Vector2 p) {
    const double raw = closestParameter(edge, p);
    const double t = std::clamp(raw, 0.0, 1.0);
    const Vector2 nearest = edge.point(t);
    const Vector2 tangent = edge.direction(t);
    const double tangentLength = std::max(math::length(tangent), 1e-12);
    const Vector2 offset = p - nearest;
    const double distance = math::length(offset);
    const double sign = math::cross(tangent, offset) >= 0 ? 1.0 : -1.0;
    double pseudo = distance;
    if (raw < 0 || raw > 1) {
        const Vector2 anchor = raw < 0 ? edge.p0 : edge.p2;
        pseudo = std::abs(math::cross(tangent, p - anchor)) / tangentLength;
    }
    const double orthogonality =
        distance > 0 ? std::abs(math::cross(tangent, offset)) / (tangentLength * distance) : 1;
    return {distance, orthogonality, sign * pseudo};
}

std::vector<std::vector<Edge>> edgesOf(const render::VectorPath &path) {
    std::vector<std::vector<Edge>> contours;
    const auto points = path.points();
    std::size_t index = 0;
    Vector2 current{0, 0};
    Vector2 start{0, 0};
    for (const auto verb : path.verbs()) {
        switch (verb) {
        case render::PathVerb::move:
            contours.emplace_back();
            current = start = points[index++];
            break;
        case render::PathVerb::line:
            contours.back().push_back({current, current, points[index], false, white});
            current = points[index++];
            break;
        case render::PathVerb::quad:
            contours.back().push_back({current, points[index], points[index + 1], true, white});
            current = points[index + 1];
            index += 2;
            break;
        case render::PathVerb::close:
            if (math::length(current - start) > 1e-9) {
                contours.back().push_back({current, current, start, false, white});
            }
            current = start;
            break;
        default:
            index += 3;
            break;
        }
    }
    return contours;
}

void colorEdges(std::vector<std::vector<Edge>> &contours) {
    constexpr std::uint8_t palette[3] = {red | green, green | blue, red | blue};
    for (auto &contour : contours) {
        std::vector<std::size_t> corners;
        for (std::size_t i = 0; i < contour.size(); ++i) {
            const Edge &previous = contour[(i + contour.size() - 1) % contour.size()];
            const Vector2 a = previous.direction(1);
            const Vector2 b = contour[i].direction(0);
            const double la = std::max(math::length(a), 1e-12);
            const double lb = std::max(math::length(b), 1e-12);
            if (math::dot(a, b) / (la * lb) < 0.996) {
                corners.push_back(i);
            }
        }
        if (corners.empty()) {
            continue;
        }
        std::size_t colorIndex = 0;
        for (std::size_t k = 0; k < contour.size(); ++k) {
            const std::size_t i = (corners[0] + k) % contour.size();
            if (k != 0 && std::find(corners.begin(), corners.end(), i) != corners.end()) {
                colorIndex = (colorIndex + 1) % 3;
                if (corners.size() > 1 && i == corners.back() && colorIndex == 0) {
                    colorIndex = 1;
                }
            }
            contour[i].color = palette[colorIndex];
        }
    }
}

double orientation(const std::vector<std::vector<Edge>> &contours) {
    double area = 0;
    for (const auto &contour : contours) {
        for (const Edge &edge : contour) {
            const int steps = edge.quadratic ? 8 : 1;
            for (int s = 0; s < steps; ++s) {
                area += math::cross(edge.point(static_cast<double>(s) / steps),
                                    edge.point(static_cast<double>(s + 1) / steps));
            }
        }
    }
    return area >= 0 ? 1.0 : -1.0;
}

bool inside(const std::vector<std::vector<Edge>> &contours, Vector2 p) {
    int winding = 0;
    for (const auto &contour : contours) {
        for (const Edge &edge : contour) {
            const int steps = edge.quadratic ? 8 : 1;
            for (int s = 0; s < steps; ++s) {
                const Vector2 a = edge.point(static_cast<double>(s) / steps);
                const Vector2 b = edge.point(static_cast<double>(s + 1) / steps);
                if ((a.y <= p.y) != (b.y <= p.y)) {
                    const double x = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
                    if (x > p.x) {
                        winding += b.y > a.y ? 1 : -1;
                    }
                }
            }
        }
    }
    return winding != 0;
}
}

void generateMtsdf(const FontFile &font, std::uint16_t glyph, const MtsdfSettings &settings,
                   GlyphBitmap &bitmap) {
    const double scale = settings.emSize / font.metrics().unitsPerEm;
    render::VectorPath path;
    font.outline(glyph, scale, path);
    auto contours = edgesOf(path);
    colorEdges(contours);
    const double factor = orientation(contours);
    double x0 = std::numeric_limits<double>::infinity();
    double y0 = x0;
    double x1 = -x0;
    double y1 = -x0;
    for (const auto &contour : contours) {
        for (const Edge &edge : contour) {
            for (const Vector2 p : {edge.p0, edge.p1, edge.p2}) {
                x0 = std::min(x0, p.x);
                y0 = std::min(y0, p.y);
                x1 = std::max(x1, p.x);
                y1 = std::max(y1, p.y);
            }
        }
    }
    if (contours.empty()) {
        bitmap = {};
        return;
    }
    const double pad = settings.spread + 1;
    const double left = std::floor(x0 - pad);
    const double top = std::floor(y0 - pad);
    const auto width = static_cast<std::uint32_t>(std::ceil(x1 + pad) - left);
    const auto height = static_cast<std::uint32_t>(std::ceil(y1 + pad) - top);
    bitmap = {width,
              height,
              4,
              static_cast<float>(left),
              static_cast<float>(top),
              static_cast<float>(settings.emSize / font.metrics().unitsPerEm),
              std::vector<float>(static_cast<std::size_t>(width) * height * 4)};
    const auto encode = [&](double distance) {
        return static_cast<float>(std::clamp(0.5 + distance / (2 * settings.spread), 0.0, 1.0));
    };
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const Vector2 p{left + x + 0.5, top + y + 0.5};
            const double sign = inside(contours, p) ? 1.0 : -1.0;
            double trueDistance = std::numeric_limits<double>::infinity();
            EdgeDistance best[3] = {{INFINITY, 0, 0}, {INFINITY, 0, 0}, {INFINITY, 0, 0}};
            for (const auto &contour : contours) {
                for (const Edge &edge : contour) {
                    const EdgeDistance measured = measure(edge, p);
                    trueDistance = std::min(trueDistance, measured.distance);
                    for (int c = 0; c < 3; ++c) {
                        if ((edge.color & (1 << c)) == 0) {
                            continue;
                        }
                        const bool closer =
                            measured.distance < best[c].distance - 1e-9 ||
                            (std::abs(measured.distance - best[c].distance) <= 1e-9 &&
                             measured.orthogonality > best[c].orthogonality);
                        if (closer) {
                            best[c] = measured;
                        }
                    }
                }
            }
            double channels[3];
            for (int c = 0; c < 3; ++c) {
                channels[c] = std::isfinite(best[c].distance) ? factor * best[c].signedPseudo
                                                              : sign * trueDistance;
            }
            const double median =
                std::max(std::min(channels[0], channels[1]),
                         std::min(std::max(channels[0], channels[1]), channels[2]));
            if ((median > 0) != (sign > 0)) {
                channels[0] = channels[1] = channels[2] = sign * trueDistance;
            }
            float *out = &bitmap.texels[(static_cast<std::size_t>(y) * width + x) * 4];
            out[0] = encode(channels[0]);
            out[1] = encode(channels[1]);
            out[2] = encode(channels[2]);
            out[3] = encode(sign * trueDistance);
        }
    }
}
}
