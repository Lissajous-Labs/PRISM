#include "text/BinaryReader.h"
#include "text/FontFile.h"

#include <optional>
#include <vector>

namespace prism::text {
namespace {
struct OutlinePoint {
    double x;
    double y;
    bool onCurve;
};

math::Vector2 mapPoint(const double t[6], double x, double y) noexcept {
    return {t[0] * x + t[2] * y + t[4], t[1] * x + t[3] * y + t[5]};
}

void emitContour(const std::vector<OutlinePoint> &points, const double t[6],
                 render::VectorPath &path) {
    if (points.empty()) {
        return;
    }
    const std::size_t count = points.size();
    std::size_t first = 0;
    while (first < count && !points[first].onCurve) {
        ++first;
    }
    math::Vector2 start;
    if (first == count) {
        start = mapPoint(t, (points[0].x + points[1 % count].x) / 2,
                         (points[0].y + points[1 % count].y) / 2);
        first = 0;
    } else {
        start = mapPoint(t, points[first].x, points[first].y);
    }
    path.moveTo(start);
    std::optional<math::Vector2> control;
    for (std::size_t k = 1; k <= count; ++k) {
        const OutlinePoint &p = points[(first + k) % count];
        const math::Vector2 mapped = mapPoint(t, p.x, p.y);
        if (p.onCurve) {
            if (control) {
                path.quadTo(*control, mapped);
                control.reset();
            } else {
                path.lineTo(mapped);
            }
        } else if (control) {
            const math::Vector2 middle = (*control + mapped) * 0.5;
            path.quadTo(*control, middle);
            control = mapped;
        } else {
            control = mapped;
        }
    }
    if (control) {
        path.quadTo(*control, start);
    }
    path.close();
}
}

void FontFile::appendOutline(std::uint16_t glyph, const double transform[6],
                             render::VectorPath &path, int depth) const {
    std::size_t length = 0;
    const std::size_t offset = glyphOffset(glyph, length);
    if (length == 0 || depth > 8) {
        return;
    }
    BinaryReader reader(bytes_, offset);
    const std::int16_t contours = reader.i16();
    reader.skip(8);
    if (contours < 0) {
        bool more = true;
        while (more && reader.ok()) {
            const std::uint16_t flags = reader.u16();
            const std::uint16_t component = reader.u16();
            double dx = 0;
            double dy = 0;
            if (flags & 0x1u) {
                dx = reader.i16();
                dy = reader.i16();
            } else {
                dx = static_cast<std::int8_t>(reader.u8());
                dy = static_cast<std::int8_t>(reader.u8());
            }
            double a = 1, b = 0, c = 0, d = 1;
            const auto f2dot14 = [&] { return reader.i16() / 16384.0; };
            if (flags & 0x8u) {
                a = d = f2dot14();
            } else if (flags & 0x40u) {
                a = f2dot14();
                d = f2dot14();
            } else if (flags & 0x80u) {
                a = f2dot14();
                b = f2dot14();
                c = f2dot14();
                d = f2dot14();
            }
            const double combined[6] = {transform[0] * a + transform[2] * b,
                                        transform[1] * a + transform[3] * b,
                                        transform[0] * c + transform[2] * d,
                                        transform[1] * c + transform[3] * d,
                                        transform[0] * dx + transform[2] * dy + transform[4],
                                        transform[1] * dx + transform[3] * dy + transform[5]};
            const std::size_t resume = reader.offset();
            appendOutline(component, combined, path, depth + 1);
            reader.seek(resume);
            more = (flags & 0x20u) != 0;
        }
        return;
    }
    std::vector<std::uint16_t> ends(static_cast<std::size_t>(contours));
    for (auto &end : ends) {
        end = reader.u16();
    }
    const std::size_t pointCount = ends.empty() ? 0 : static_cast<std::size_t>(ends.back()) + 1;
    reader.skip(reader.u16());
    std::vector<std::uint8_t> flags;
    flags.reserve(pointCount);
    while (flags.size() < pointCount && reader.ok()) {
        const std::uint8_t flag = reader.u8();
        flags.push_back(flag);
        if (flag & 0x8u) {
            const std::uint8_t repeat = reader.u8();
            flags.insert(flags.end(), repeat, flag);
        }
    }
    flags.resize(pointCount);
    std::vector<OutlinePoint> points(pointCount);
    double value = 0;
    for (std::size_t i = 0; i < pointCount; ++i) {
        if (flags[i] & 0x2u) {
            value += (flags[i] & 0x10u) ? reader.u8() : -static_cast<double>(reader.u8());
        } else if (!(flags[i] & 0x10u)) {
            value += reader.i16();
        }
        points[i].x = value;
        points[i].onCurve = (flags[i] & 0x1u) != 0;
    }
    value = 0;
    for (std::size_t i = 0; i < pointCount; ++i) {
        if (flags[i] & 0x4u) {
            value += (flags[i] & 0x20u) ? reader.u8() : -static_cast<double>(reader.u8());
        } else if (!(flags[i] & 0x20u)) {
            value += reader.i16();
        }
        points[i].y = value;
    }
    if (!reader.ok()) {
        return;
    }
    std::size_t begin = 0;
    for (const std::uint16_t end : ends) {
        emitContour(
            std::vector<OutlinePoint>(points.begin() + static_cast<std::ptrdiff_t>(begin),
                                      points.begin() + static_cast<std::ptrdiff_t>(end) + 1),
            transform, path);
        begin = static_cast<std::size_t>(end) + 1;
    }
}
}
