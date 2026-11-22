#include "text/MtsdfGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>

namespace {
using namespace prism;
using math::Vector2;

struct Shape {
    std::string name;
    render::VectorPath path;
    std::vector<std::function<Vector2(double)>> edges;
};

Vector2 quadratic(Vector2 a, Vector2 b, Vector2 c, double t) {
    const double s = 1 - t;
    return a * (s * s) + b * (2 * s * t) + c * (t * t);
}

Vector2 cubic(Vector2 a, Vector2 b, Vector2 c, Vector2 d, double t) {
    const double s = 1 - t;
    return a * (s * s * s) + b * (3 * s * s * t) + c * (3 * s * t * t) + d * (t * t * t);
}

void addCircle(Shape &shape, Vector2 centre, double radius, bool reverse) {
    constexpr int pieces = 8;
    const double step = (reverse ? -2 : 2) * std::numbers::pi / pieces;
    const auto at = [&](double angle, double r) {
        return centre + Vector2{std::cos(angle), std::sin(angle)} * r;
    };
    shape.path.moveTo(at(0, radius));
    for (int i = 0; i < pieces; ++i) {
        const double a0 = step * i;
        const double a1 = a0 + step;
        const Vector2 p0 = at(a0, radius);
        const Vector2 p1 = at((a0 + a1) / 2, radius / std::cos(step / 2));
        const Vector2 p2 = at(a1, radius);
        shape.path.quadTo(p1, p2);
        shape.edges.emplace_back([=](double t) { return quadratic(p0, p1, p2, t); });
    }
    shape.path.close();
}

void addPolygon(Shape &shape, const std::vector<Vector2> &points) {
    shape.path.moveTo(points[0]);
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Vector2 a = points[i];
        const Vector2 b = points[(i + 1) % points.size()];
        if (i + 1 < points.size()) {
            shape.path.lineTo(b);
        }
        shape.edges.emplace_back([=](double t) { return a + (b - a) * t; });
    }
    shape.path.close();
}

std::vector<Shape> shapes() {
    std::vector<Shape> list;

    Shape ring{"ring", {}, {}};
    addCircle(ring, {24, 24}, 18, false);
    addCircle(ring, {24, 24}, 9, true);
    list.push_back(std::move(ring));

    Shape spike{"spike", {}, {}};
    addPolygon(spike, {{6, 42}, {24, 4}, {42, 42}, {24, 30}});
    list.push_back(std::move(spike));

    Shape wave{"wave", {}, {}};
    const Vector2 a{6, 30};
    const Vector2 b{14, 6};
    const Vector2 c{34, 54};
    const Vector2 d{42, 18};
    wave.path.moveTo(a);
    wave.path.cubicTo(b, c, d);
    wave.path.lineTo({42, 42});
    wave.path.lineTo({6, 42});
    wave.path.close();
    wave.edges.emplace_back([=](double t) { return cubic(a, b, c, d, t); });
    wave.edges.emplace_back([=](double t) { return d + (Vector2{42, 42} - d) * t; });
    wave.edges.emplace_back(
        [=](double t) { return Vector2{42, 42} + (Vector2{6, 42} - Vector2{42, 42}) * t; });
    wave.edges.emplace_back([=](double t) { return Vector2{6, 42} + (a - Vector2{6, 42}) * t; });
    list.push_back(std::move(wave));

    Shape tear{"teardrop", {}, {}};
    const Vector2 apex{24, 4};
    const Vector2 c1{52, 40};
    const Vector2 c2{-4, 40};
    tear.path.moveTo(apex);
    tear.path.cubicTo(c1, c2, apex);
    tear.path.close();
    tear.edges.emplace_back([=](double t) { return cubic(apex, c1, c2, apex, t); });
    list.push_back(std::move(tear));
    return list;
}

double sampleMedian(const text::GlyphBitmap &bitmap, Vector2 p) {
    const double fx = p.x - bitmap.left - 0.5;
    const double fy = p.y - bitmap.top - 0.5;
    const auto x0 = static_cast<int>(std::floor(fx));
    const auto y0 = static_cast<int>(std::floor(fy));
    const double tx = fx - x0;
    const double ty = fy - y0;
    std::array<double, 3> channels{};
    for (int c = 0; c < 3; ++c) {
        const auto texel = [&](int x, int y) {
            x = std::clamp(x, 0, static_cast<int>(bitmap.width) - 1);
            y = std::clamp(y, 0, static_cast<int>(bitmap.height) - 1);
            return static_cast<double>(
                bitmap.texels[(static_cast<std::size_t>(y) * bitmap.width + x) * 4 + c]);
        };
        const double top = texel(x0, y0) * (1 - tx) + texel(x0 + 1, y0) * tx;
        const double bottom = texel(x0, y0 + 1) * (1 - tx) + texel(x0 + 1, y0 + 1) * tx;
        channels[c] = top * (1 - ty) + bottom * ty;
    }
    return std::max(std::min(channels[0], channels[1]),
                    std::min(std::max(channels[0], channels[1]), channels[2]));
}
}

int main() {
    const text::MtsdfSettings settings;
    double worstRms = 0;
    for (const Shape &shape : shapes()) {
        text::GlyphBitmap bitmap;
        text::generateMtsdf(shape.path, settings, bitmap);
        double sum = 0;
        double peak = 0;
        std::size_t count = 0;
        for (const auto &edge : shape.edges) {
            for (int i = 0; i <= 400; ++i) {
                const Vector2 q = edge(i / 400.0);
                const double error =
                    std::abs((sampleMedian(bitmap, q) - 0.5) * 2 * settings.spread);
                sum += error * error;
                peak = std::max(peak, error);
                ++count;
            }
        }
        const double rms = std::sqrt(sum / static_cast<double>(count));
        worstRms = std::max(worstRms, rms);
        std::cout << shape.name << ": contour RMS " << rms << " px, max " << peak << " px at "
                  << settings.emSize << " ppem\n";
    }
    return worstRms < 0.03 ? 0 : 1;
}
