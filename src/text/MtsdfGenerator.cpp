#include "text/MtsdfGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace prism::text {
namespace {
using math::Vector2;

enum Channel : std::uint8_t {
    black = 0,
    red = 1,
    green = 2,
    yellow = 3,
    blue = 4,
    magenta = 5,
    cyan = 6,
    white = 7
};

constexpr double cornerCross = 0.052335956242943835;
constexpr double rootEpsilon = 1e-12;

enum class Order : std::uint8_t { line, quadratic, cubic };

struct Edge {
    std::array<Vector2, 4> p{};
    Order order = Order::line;
    std::uint8_t color = white;

    [[nodiscard]] Vector2 start() const noexcept { return p[0]; }
    [[nodiscard]] Vector2 end() const noexcept {
        return order == Order::line ? p[1] : order == Order::quadratic ? p[2] : p[3];
    }

    [[nodiscard]] Vector2 point(double t) const noexcept {
        const double s = 1 - t;
        switch (order) {
        case Order::line:
            return p[0] * s + p[1] * t;
        case Order::quadratic:
            return p[0] * (s * s) + p[1] * (2 * s * t) + p[2] * (t * t);
        case Order::cubic:
            return p[0] * (s * s * s) + p[1] * (3 * s * s * t) + p[2] * (3 * s * t * t) +
                   p[3] * (t * t * t);
        }
        return p[0];
    }

    [[nodiscard]] Vector2 direction(double t) const noexcept {
        const double s = 1 - t;
        switch (order) {
        case Order::line:
            return p[1] - p[0];
        case Order::quadratic:
            return (p[1] - p[0]) * (2 * s) + (p[2] - p[1]) * (2 * t);
        case Order::cubic: {
            const Vector2 d = (p[1] - p[0]) * (3 * s * s) + (p[2] - p[1]) * (6 * s * t) +
                              (p[3] - p[2]) * (3 * t * t);
            if (math::dot(d, d) > 0) {
                return d;
            }
            return t < 0.5 ? p[2] - p[0] : p[3] - p[1];
        }
        }
        return p[1] - p[0];
    }

    [[nodiscard]] Vector2 second(double t) const noexcept {
        switch (order) {
        case Order::line:
            return {0, 0};
        case Order::quadratic:
            return (p[2] - p[1] * 2 + p[0]) * 2;
        case Order::cubic:
            return (p[2] - p[1] * 2 + p[0]) * (6 * (1 - t)) + (p[3] - p[2] * 2 + p[1]) * (6 * t);
        }
        return {0, 0};
    }

    [[nodiscard]] Vector2 third() const noexcept {
        return order == Order::cubic ? (p[3] - p[2] * 3 + p[1] * 3 - p[0]) * 6 : Vector2{0, 0};
    }

    void split(Edge &first, Edge &middle, Edge &last) const noexcept {
        const auto piece = [&](double a, double b) {
            Edge e;
            e.order = order;
            e.color = color;
            if (order == Order::line) {
                e.p[0] = point(a);
                e.p[1] = point(b);
                return e;
            }
            const double h = b - a;
            const Vector2 pa = point(a);
            const Vector2 pb = point(b);
            if (order == Order::quadratic) {
                e.p[0] = pa;
                e.p[1] = pa + direction(a) * (h / 2);
                e.p[2] = pb;
                return e;
            }
            e.p[0] = pa;
            e.p[1] = pa + direction(a) * (h / 3);
            e.p[2] = pb - direction(b) * (h / 3);
            e.p[3] = pb;
            return e;
        };
        first = piece(0, 1.0 / 3);
        middle = piece(1.0 / 3, 2.0 / 3);
        last = piece(2.0 / 3, 1);
    }
};

struct EdgeDistance {
    double distance = std::numeric_limits<double>::infinity();
    double orthogonality = 0;
    double parameter = 0;
};

int solveQuadratic(double a, double b, double c, std::array<double, 3> &roots) {
    if (std::abs(a) < rootEpsilon * (std::abs(b) + std::abs(c) + 1)) {
        if (std::abs(b) < rootEpsilon) {
            return 0;
        }
        roots[0] = -c / b;
        return 1;
    }
    const double discriminant = b * b - 4 * a * c;
    if (discriminant < 0) {
        return 0;
    }
    if (discriminant == 0) {
        roots[0] = -b / (2 * a);
        return 1;
    }
    const double q = -0.5 * (b + std::copysign(std::sqrt(discriminant), b));
    roots[0] = q / a;
    roots[1] = c / q;
    return 2;
}

int solveCubic(double a, double b, double c, double d, std::array<double, 3> &roots) {
    const double scale = std::abs(b) + std::abs(c) + std::abs(d);
    if (std::abs(a) < rootEpsilon * (scale + 1)) {
        return solveQuadratic(b, c, d, roots);
    }

    const double A = b / a;
    const double B = c / a;
    const double C = d / a;
    const double q = (A * A - 3 * B) / 9;
    const double r = (A * (2 * A * A - 9 * B) + 27 * C) / 54;
    const double q3 = q * q * q;

    if (r * r < q3) {
        const double theta = std::acos(std::clamp(r / std::sqrt(q3), -1.0, 1.0));
        const double m = -2 * std::sqrt(q);
        roots[0] = m * std::cos(theta / 3) - A / 3;
        roots[1] = m * std::cos((theta + 2 * std::numbers::pi) / 3) - A / 3;
        roots[2] = m * std::cos((theta - 2 * std::numbers::pi) / 3) - A / 3;
        return 3;
    }

    const double u = -std::copysign(std::cbrt(std::abs(r) + std::sqrt(r * r - q3)), r);
    const double v = u == 0 ? 0 : q / u;
    roots[0] = u + v - A / 3;
    if (std::abs(u - v) < 1e-14 * (std::abs(u) + 1)) {
        roots[1] = -0.5 * (u + v) - A / 3;
        return 2;
    }
    return 1;
}

EdgeDistance consider(const Edge &edge, Vector2 p, double t, EdgeDistance best) {
    const double clamped = std::clamp(t, 0.0, 1.0);
    const double distance = math::length(edge.point(clamped) - p);
    if (distance < best.distance) {
        best.distance = distance;
        best.parameter = clamped;
    }
    return best;
}

double halleyRefine(const Edge &edge, Vector2 p, double t) {
    for (int step = 0; step < 3; ++step) {
        const Vector2 offset = edge.point(t) - p;
        const Vector2 d1 = edge.direction(t);
        const Vector2 d2 = edge.second(t);
        const double f = math::dot(offset, d1);
        const double f1 = math::dot(d1, d1) + math::dot(offset, d2);
        const double f2 = 3 * math::dot(d1, d2) + math::dot(offset, edge.third());
        const double denominator = 2 * f1 * f1 - f * f2;
        if (!(std::abs(denominator) > 1e-300)) {
            break;
        }
        const double delta = std::clamp(2 * f * f1 / denominator, -0.25, 0.25);
        t = std::clamp(t - delta, 0.0, 1.0);
    }
    return t;
}

EdgeDistance closest(const Edge &edge, Vector2 p) {
    EdgeDistance best;
    best = consider(edge, p, 0, best);
    best = consider(edge, p, 1, best);

    switch (edge.order) {
    case Order::line: {
        const Vector2 d = edge.p[1] - edge.p[0];
        const double len = math::dot(d, d);
        if (len > 0) {
            best = consider(edge, p, math::dot(p - edge.p[0], d) / len, best);
        }
        break;
    }
    case Order::quadratic: {
        const Vector2 a = edge.p[2] - edge.p[1] * 2 + edge.p[0];
        const Vector2 b = edge.p[1] - edge.p[0];
        const Vector2 c = edge.p[0] - p;
        std::array<double, 3> roots{};
        const int count = solveCubic(math::dot(a, a), 3 * math::dot(a, b),
                                     2 * math::dot(b, b) + math::dot(a, c), math::dot(b, c), roots);
        for (int i = 0; i < count; ++i) {
            if (roots[i] > 0 && roots[i] < 1) {
                best = consider(edge, p, roots[i], best);
            }
        }
        break;
    }
    case Order::cubic: {
        constexpr int starts = 8;
        for (int i = 0; i <= starts; ++i) {
            best = consider(edge, p, halleyRefine(edge, p, static_cast<double>(i) / starts), best);
        }
        break;
    }
    }

    const Vector2 tangent = edge.direction(best.parameter);
    const Vector2 offset = p - edge.point(best.parameter);
    const double tangentLength = math::length(tangent);
    best.orthogonality =
        best.distance > 0 && tangentLength > 0
            ? std::abs(math::cross(tangent, offset)) / (tangentLength * best.distance)
            : 1;
    return best;
}

double signedPseudoDistance(const Edge &edge, Vector2 p, const EdgeDistance &measured) {
    const double t = measured.parameter;
    const Vector2 tangent = edge.direction(t);
    const Vector2 offset = p - edge.point(t);
    const double sign = math::cross(tangent, offset) >= 0 ? 1.0 : -1.0;
    double distance = measured.distance;
    const double tangentLength = math::length(tangent);

    if (tangentLength > 0 && (t <= 0 || t >= 1)) {
        const Vector2 unit = tangent * (1 / tangentLength);
        const double along = math::dot(offset, unit);
        if ((t <= 0 && along < 0) || (t >= 1 && along > 0)) {
            const double perpendicular = math::cross(unit, offset);
            if (std::abs(perpendicular) <= distance) {
                return perpendicular;
            }
        }
    }
    return sign * distance;
}

int crossings(const Edge &edge, Vector2 p) {
    std::array<double, 3> roots{};
    int count = 0;
    switch (edge.order) {
    case Order::line:
        count = solveQuadratic(0, edge.p[1].y - edge.p[0].y, edge.p[0].y - p.y, roots);
        break;
    case Order::quadratic:
        count = solveQuadratic(edge.p[0].y - 2 * edge.p[1].y + edge.p[2].y,
                               2 * (edge.p[1].y - edge.p[0].y), edge.p[0].y - p.y, roots);
        break;
    case Order::cubic: {
        const double y0 = edge.p[0].y;
        const double y1 = edge.p[1].y;
        const double y2 = edge.p[2].y;
        const double y3 = edge.p[3].y;
        count = solveCubic(-y0 + 3 * y1 - 3 * y2 + y3, 3 * y0 - 6 * y1 + 3 * y2, -3 * y0 + 3 * y1,
                           y0 - p.y, roots);
        break;
    }
    }

    int winding = 0;
    for (int i = 0; i < count; ++i) {
        const double t = roots[i];
        if (t < 0 || t >= 1) {
            continue;
        }
        const double dy = edge.direction(t).y;
        if (dy == 0 || edge.point(t).x <= p.x) {
            continue;
        }
        winding += dy > 0 ? 1 : -1;
    }
    return winding;
}

using Contours = std::vector<std::vector<Edge>>;

bool inside(const Contours &contours, Vector2 p) {
    int winding = 0;
    for (const auto &contour : contours) {
        for (const Edge &edge : contour) {
            winding += crossings(edge, p);
        }
    }
    return winding != 0;
}

void appendArc(std::vector<Edge> &contour, Vector2 centre, double radius, double start,
               double sweep) {
    const int pieces =
        std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / (std::numbers::pi / 4))));
    const double step = sweep / pieces;
    for (int i = 0; i < pieces; ++i) {
        const double a0 = start + step * i;
        const double a1 = a0 + step;
        const double mid = (a0 + a1) / 2;
        const double reach = radius / std::cos(step / 2);
        Edge edge;
        edge.order = Order::quadratic;
        edge.p[0] = centre + Vector2{std::cos(a0), std::sin(a0)} * radius;
        edge.p[1] = centre + Vector2{std::cos(mid), std::sin(mid)} * reach;
        edge.p[2] = centre + Vector2{std::cos(a1), std::sin(a1)} * radius;
        contour.push_back(edge);
    }
}

Contours edgesOf(const render::VectorPath &path) {
    Contours contours;
    const auto points = path.points();
    std::size_t index = 0;
    Vector2 current{0, 0};
    Vector2 start{0, 0};
    const auto close = [&] {
        if (!contours.empty() && math::length(current - start) > 1e-9) {
            Edge edge;
            edge.p[0] = current;
            edge.p[1] = start;
            contours.back().push_back(edge);
        }
        current = start;
    };

    for (const auto verb : path.verbs()) {
        switch (verb) {
        case render::PathVerb::move:
            if (!contours.empty() && !contours.back().empty()) {
                close();
            }
            contours.emplace_back();
            current = start = points[index++];
            break;
        case render::PathVerb::line: {
            Edge edge;
            edge.p[0] = current;
            edge.p[1] = points[index];
            contours.back().push_back(edge);
            current = points[index++];
            break;
        }
        case render::PathVerb::quad: {
            Edge edge;
            edge.order = Order::quadratic;
            edge.p[0] = current;
            edge.p[1] = points[index];
            edge.p[2] = points[index + 1];
            contours.back().push_back(edge);
            current = points[index + 1];
            index += 2;
            break;
        }
        case render::PathVerb::cubic: {
            Edge edge;
            edge.order = Order::cubic;
            edge.p[0] = current;
            edge.p[1] = points[index];
            edge.p[2] = points[index + 1];
            edge.p[3] = points[index + 2];
            contours.back().push_back(edge);
            current = points[index + 2];
            index += 3;
            break;
        }
        case render::PathVerb::arc: {
            const Vector2 centre = points[index];
            const double radius = points[index + 1].x;
            const double from = points[index + 2].x;
            const double sweep = points[index + 2].y;
            appendArc(contours.back(), centre, radius, from, sweep);
            current = centre + Vector2{std::cos(from + sweep), std::sin(from + sweep)} * radius;
            index += 3;
            break;
        }
        case render::PathVerb::close:
            close();
            break;
        }
    }
    if (!contours.empty() && !contours.back().empty()) {
        close();
    }
    std::erase_if(contours, [](const auto &contour) { return contour.empty(); });
    return contours;
}

bool isCorner(Vector2 a, Vector2 b) {
    const double la = math::length(a);
    const double lb = math::length(b);
    if (la == 0 || lb == 0) {
        return false;
    }
    const Vector2 ua = a * (1 / la);
    const Vector2 ub = b * (1 / lb);
    return math::dot(ua, ub) <= 0 || std::abs(math::cross(ua, ub)) > cornerCross;
}

void switchColor(std::uint8_t &color, std::uint8_t banned = black) {
    const auto combined = static_cast<std::uint8_t>(color & banned);
    if (combined == red || combined == green || combined == blue) {
        color = static_cast<std::uint8_t>(combined ^ white);
        return;
    }
    if (color == black || color == white) {
        color = cyan;
        return;
    }
    const int shifted = color << 1;
    color = static_cast<std::uint8_t>((shifted | shifted >> 3) & white);
}

void colorEdges(Contours &contours) {
    for (auto &contour : contours) {
        std::vector<std::size_t> corners;
        for (std::size_t i = 0; i < contour.size(); ++i) {
            const Edge &previous = contour[(i + contour.size() - 1) % contour.size()];
            if (isCorner(previous.direction(1), contour[i].direction(0))) {
                corners.push_back(i);
            }
        }

        if (corners.empty()) {
            for (Edge &edge : contour) {
                edge.color = white;
            }
            continue;
        }

        if (corners.size() == 1) {
            std::array<std::uint8_t, 3> colors{white, white, white};
            switchColor(colors[0]);
            colors[2] = colors[0];
            switchColor(colors[2]);
            const std::size_t corner = corners[0];
            if (contour.size() < 3) {
                std::vector<Edge> parts;
                for (std::size_t k = 0; k < contour.size(); ++k) {
                    Edge a;
                    Edge b;
                    Edge c;
                    contour[(corner + k) % contour.size()].split(a, b, c);
                    parts.insert(parts.end(), {a, b, c});
                }
                contour = std::move(parts);
            } else {
                std::rotate(contour.begin(), contour.begin() + static_cast<std::ptrdiff_t>(corner),
                            contour.end());
            }
            const std::size_t m = contour.size();
            for (std::size_t i = 0; i < m; ++i) {
                const auto slot =
                    static_cast<int>(3 +
                                     2.875 * static_cast<double>(i) / static_cast<double>(m - 1) -
                                     1.4375 + 0.5) -
                    3;
                contour[i].color = colors[static_cast<std::size_t>(std::clamp(slot + 1, 0, 2))];
            }
            continue;
        }

        std::uint8_t color = white;
        switchColor(color);
        const std::uint8_t initial = color;
        std::size_t spline = 0;
        const std::size_t start = corners[0];
        const std::size_t m = contour.size();
        for (std::size_t i = 0; i < m; ++i) {
            const std::size_t index = (start + i) % m;
            if (spline + 1 < corners.size() && corners[spline + 1] == index) {
                ++spline;
                switchColor(color, spline == corners.size() - 1 ? initial : black);
            }
            contour[index].color = color;
        }
    }
}

double orientation(const Contours &contours) {
    double area = 0;
    for (const auto &contour : contours) {
        for (const Edge &edge : contour) {
            constexpr int steps = 16;
            for (int s = 0; s < steps; ++s) {
                area += math::cross(edge.point(static_cast<double>(s) / steps),
                                    edge.point(static_cast<double>(s + 1) / steps));
            }
        }
    }
    return area >= 0 ? 1.0 : -1.0;
}

float median(const float *texel) noexcept {
    return std::max(std::min(texel[0], texel[1]), std::min(std::max(texel[0], texel[1]), texel[2]));
}

void correctErrors(GlyphBitmap &bitmap, double spread) {
    const std::uint32_t width = bitmap.width;
    const std::uint32_t height = bitmap.height;
    std::vector<std::uint8_t> clash(static_cast<std::size_t>(width) * height, 0);
    const auto at = [&](std::uint32_t x, std::uint32_t y) {
        return &bitmap.texels[(static_cast<std::size_t>(y) * width + x) * 4];
    };
    const auto margin = static_cast<float>(0.5 / spread);
    const auto inconsistent = [margin](const float *a, const float *b, float wa) {
        float mixed[3];
        for (int c = 0; c < 3; ++c) {
            mixed[c] = a[c] * wa + b[c] * (1 - wa);
        }
        const float truth = a[3] * wa + b[3] * (1 - wa);
        const float m = median(mixed);
        return (m - 0.5f) * (truth - 0.5f) < 0 && std::abs(truth - 0.5f) > margin;
    };

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const float *here = at(x, y);
            if ((median(here) - 0.5f) * (here[3] - 0.5f) < 0) {
                clash[static_cast<std::size_t>(y) * width + x] = 1;
            }
            const std::array<std::pair<int, int>, 4> neighbours{{{1, 0}, {0, 1}, {1, 1}, {1, -1}}};
            for (const auto &[dx, dy] : neighbours) {
                const auto nx = static_cast<std::int64_t>(x) + dx;
                const auto ny = static_cast<std::int64_t>(y) + dy;
                if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                    continue;
                }
                const float *there =
                    at(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny));
                for (const float w : {0.25f, 0.5f, 0.75f}) {
                    if (inconsistent(here, there, w)) {
                        clash[static_cast<std::size_t>(y) * width + x] = 1;
                        clash[static_cast<std::size_t>(ny) * width + static_cast<std::size_t>(nx)] =
                            1;
                        break;
                    }
                }
            }
        }
    }

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            if (clash[static_cast<std::size_t>(y) * width + x] != 0) {
                float *texel = at(x, y);
                texel[0] = texel[1] = texel[2] = texel[3];
            }
        }
    }
}
}

void generateMtsdf(const render::VectorPath &outline, const MtsdfSettings &settings,
                   GlyphBitmap &bitmap) {
    auto contours = edgesOf(outline);
    if (contours.empty()) {
        bitmap = {};
        return;
    }
    colorEdges(contours);
    const double factor = orientation(contours);

    double x0 = std::numeric_limits<double>::infinity();
    double y0 = x0;
    double x1 = -x0;
    double y1 = -x0;
    for (const auto &contour : contours) {
        for (const Edge &edge : contour) {
            for (const Vector2 q : edge.p) {
                x0 = std::min(x0, q.x);
                y0 = std::min(y0, q.y);
                x1 = std::max(x1, q.x);
                y1 = std::max(y1, q.y);
            }
        }
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
              1,
              std::vector<float>(static_cast<std::size_t>(width) * height * 4)};
    const auto encode = [&](double distance) {
        return static_cast<float>(std::clamp(0.5 + distance / (2 * settings.spread), 0.0, 1.0));
    };

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const Vector2 p{left + x + 0.5, top + y + 0.5};
            const double sign = inside(contours, p) ? 1.0 : -1.0;
            double trueDistance = std::numeric_limits<double>::infinity();
            std::array<EdgeDistance, 3> best{};
            std::array<const Edge *, 3> owner{};

            for (const auto &contour : contours) {
                for (const Edge &edge : contour) {
                    const EdgeDistance measured = closest(edge, p);
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
                            owner[c] = &edge;
                        }
                    }
                }
            }

            float *out = &bitmap.texels[(static_cast<std::size_t>(y) * width + x) * 4];
            for (int c = 0; c < 3; ++c) {
                const double value = owner[c] != nullptr
                                         ? factor * signedPseudoDistance(*owner[c], p, best[c])
                                         : sign * trueDistance;
                out[c] = encode(value);
            }
            out[3] = encode(sign * trueDistance);
        }
    }

    correctErrors(bitmap, settings.spread);
}

void generateMtsdf(const FontFile &font, std::uint16_t glyph, const MtsdfSettings &settings,
                   GlyphBitmap &bitmap) {
    const double scale = settings.emSize / font.metrics().unitsPerEm;
    render::VectorPath path;
    font.outline(glyph, scale, path);
    generateMtsdf(path, settings, bitmap);
    bitmap.scale = static_cast<float>(scale);
}
}
