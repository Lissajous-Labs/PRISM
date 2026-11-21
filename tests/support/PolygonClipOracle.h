#pragma once

#include "math/Vector2.h"

#include <cmath>
#include <span>
#include <vector>

namespace prism::test {
inline double clippedSignedArea(std::span<const math::Vector2> contour, double x0, double y0) {
    std::vector<math::Vector2> polygon(contour.begin(), contour.end());
    std::vector<math::Vector2> next;
    const auto clip = [&](auto inside, auto intersect) {
        next.clear();
        for (std::size_t i = 0; i < polygon.size(); ++i) {
            const math::Vector2 current = polygon[i];
            const math::Vector2 following = polygon[(i + 1) % polygon.size()];
            const bool currentIn = inside(current);
            const bool followingIn = inside(following);
            if (currentIn) {
                next.push_back(current);
            }
            if (currentIn != followingIn) {
                next.push_back(intersect(current, following));
            }
        }
        polygon.swap(next);
    };
    const auto vertical = [](double edge, bool keepGreater) {
        return std::pair{[=](math::Vector2 p) { return keepGreater ? p.x >= edge : p.x <= edge; },
                         [=](math::Vector2 a, math::Vector2 b) {
                             const double t = (edge - a.x) / (b.x - a.x);
                             return math::Vector2{edge, a.y + (b.y - a.y) * t};
                         }};
    };
    const auto horizontal = [](double edge, bool keepGreater) {
        return std::pair{[=](math::Vector2 p) { return keepGreater ? p.y >= edge : p.y <= edge; },
                         [=](math::Vector2 a, math::Vector2 b) {
                             const double t = (edge - a.y) / (b.y - a.y);
                             return math::Vector2{a.x + (b.x - a.x) * t, edge};
                         }};
    };
    for (const auto &[inside, intersect] : {vertical(x0, true), vertical(x0 + 1, false)}) {
        clip(inside, intersect);
    }
    for (const auto &[inside, intersect] : {horizontal(y0, true), horizontal(y0 + 1, false)}) {
        clip(inside, intersect);
    }
    double area = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        area += math::cross(polygon[i], polygon[(i + 1) % polygon.size()]);
    }
    return area / 2;
}

inline double pixelWinding(std::span<const std::vector<math::Vector2>> contours, int x, int y) {
    double winding = 0;
    for (const auto &contour : contours) {
        winding -= clippedSignedArea(contour, x, y);
    }
    return winding;
}
}
