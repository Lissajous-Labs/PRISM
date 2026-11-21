#include "render/HalftoneField.h"

#include "math/Sdf.h"
#include "math/SpotCoverage.h"
#include "math/ThermalPalette.h"
#include "render/ShadingSupport.h"
#include "render/TextureSampling.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace prism::render {
namespace {
double smooth(double lo, double hi, double value) noexcept {
    return math::thermalSmoothstep(lo, hi, value);
}
double prefix(const TextureView &texture, double u, double v) noexcept {
    const double x = std::clamp(u, 0.0, 1.0) * (texture.width - 1);
    const double y = std::clamp(v, 0.0, 1.0) * (texture.height - 1);
    const auto ix = static_cast<std::int64_t>(std::floor(x));
    const auto iy = static_cast<std::int64_t>(std::floor(y));
    const double tx = x - std::floor(x), ty = y - std::floor(y);
    const double a = fetchTexel(texture, ix, iy)[0], b = fetchTexel(texture, ix + 1, iy)[0];
    const double c = fetchTexel(texture, ix, iy + 1)[0], d = fetchTexel(texture, ix + 1, iy + 1)[0];
    return (a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty;
}
double cellEnergy(const TextureView &texture, const HalftonePayload &h, math::Vector2 center,
                  double px, double py) noexcept {
    const double width = h.rect[2] - h.rect[0], height = h.rect[3] - h.rect[1];
    const double u = (center.x - h.rect[0]) / width, v = (center.y - h.rect[1]) / height;
    if ((h.modeFlags & halftoneFlags::summedArea) == 0) {
        return sampleBilinear(texture, u, v)[0];
    }
    const double u0 = std::clamp(u - px / (2 * width), 0.0, 1.0);
    const double u1 = std::clamp(u + px / (2 * width), 0.0, 1.0);
    const double v0 = std::clamp(v - py / (2 * height), 0.0, 1.0);
    const double v1 = std::clamp(v + py / (2 * height), 0.0, 1.0);
    const double area = (u1 - u0) * (v1 - v0) * (texture.width - 1) * (texture.height - 1);
    return area > 0 ? std::clamp((prefix(texture, u1, v1) - prefix(texture, u0, v1) -
                                  prefix(texture, u1, v0) + prefix(texture, u0, v0)) /
                                     area,
                                 0.0, 1.0)
                    : 0;
}
math::LinearRgb palette(const FrameView &frame, std::uint32_t row, double energy) noexcept {
    const auto offset = static_cast<std::size_t>(row) * 1024;
    if (offset + 1024 > frame.gradientRows.size()) {
        return {};
    }
    const double x = std::clamp(energy, 0.0, 1.0) * 255;
    const auto a = static_cast<std::size_t>(x), b = std::min(a + 1, std::size_t{255});
    const double t = x - static_cast<double>(a);
    const auto channel = [&](std::size_t c) {
        return frame.gradientRows[offset + 4 * a + c] * (1 - t) +
               frame.gradientRows[offset + 4 * b + c] * t;
    };
    return {channel(0), channel(1), channel(2)};
}
}

static math::PremultipliedColor shadeHalftoneCell(const FrameView &frame, const PrimitiveDesc &desc,
                                                  const TransformRecord &transform,
                                                  math::Vector2 local, const HalftonePayload &h,
                                                  const math::HalftoneLattice &lattice,
                                                  const math::HalftoneCell &cell, double eta,
                                                  bool primary) noexcept {
    const double px = std::hypot(lattice.b1.x, lattice.b1.y);
    const double py = lattice.area() / px;
    const math::Vector2 axis{lattice.b1.x / px, lattice.b1.y / px};
    const double halfX = std::max(0.0, (px - h.gutter[0]) * 0.5);
    const double halfY = std::max(0.0, (py - h.gutter[1]) * 0.5);
    const double reach = 0.71 * std::sqrt(transform.inverse[0] * transform.inverse[0] +
                                          transform.inverse[1] * transform.inverse[1] +
                                          transform.inverse[2] * transform.inverse[2] +
                                          transform.inverse[3] * transform.inverse[3]);
    const auto delta = local - cell.center;
    if (!primary && (eta == 0 || std::abs(delta.x * axis.x + delta.y * axis.y) > halfX + reach ||
                     std::abs(-delta.x * axis.y + delta.y * axis.x) > halfY + reach)) {
        return {0, 0, 0, 0};
    }
    const auto &texture = frame.textures[desc.auxHandle];
    double energy = 0, ridge = 0;
    if ((h.modeFlags & halftoneFlags::spectralMatrix) != 0) {
        const double u = (cell.center.x - h.rect[0]) / (h.rect[2] - h.rect[0]);
        const auto band = sampleBilinear(texture, u, 0.5);
        const double rows = (h.rect[3] - h.rect[1]) / py;
        const double j = std::floor((h.rect[3] - cell.center.y) / py);
        const double rh = band[0] * rows;
        const double fill = std::clamp(rh - j, 0.0, 1.0);
        const double xi = std::clamp((j + 0.5) / std::max(rh, 1.0), 0.0, 1.0);
        const double body = fill * (0.14 + 0.86 * std::pow(xi, 1.65)) *
                            (0.32 + 0.68 * std::pow(std::max(0.0, band[0]), 0.55));
        const double cap = 0.48 * smooth(0, 0.02, band[0]) *
                           std::exp(-std::pow(j + 1 - rh, 2) / (2 * 0.55 * 0.55));
        const double peak = 0.96 * smooth(0, 0.04, band[1]) *
                            std::exp(-std::pow(j + 1 - band[1] * rows, 2) / (2 * 0.36 * 0.36));
        const double node =
            fill * 0.55 * band[2] * std::exp(-std::pow(j + 0.5 - 0.62 * rh, 2) / 0.72);
        energy = std::clamp(std::max(body + cap + node, peak), 0.0, 1.0);
        ridge = energy;
    } else {
        energy = cellEnergy(texture, h, cell.center, px, py);
        ridge =
            0.25 * cellEnergy(texture, h,
                              {cell.center.x - lattice.b1.x, cell.center.y - lattice.b1.y}, px,
                              py) +
            0.5 * energy +
            0.25 * cellEnergy(texture, h,
                              {cell.center.x + lattice.b1.x, cell.center.y + lattice.b1.y}, px, py);
        if ((h.modeFlags & halftoneFlags::cursor) != 0) {
            const double u = (cell.center.x - h.rect[0]) / (h.rect[2] - h.rect[0]);
            const double delta = (u - h.cursorParams[0]) / h.cursorParams[2];
            energy =
                std::min(1.0, energy * (1 + h.cursorParams[1] * std::exp(-0.5 * delta * delta)));
        }
    }
    double threshold = 0;
    if (h.blueNoiseSlot < frame.textures.size()) {
        threshold =
            fetchTexel(frame.textures[h.blueNoiseSlot], static_cast<std::uint32_t>(cell.x) & 63u,
                       static_cast<std::uint32_t>(cell.y) & 63u)[0] *
            (255.0 / 256);
    }
    const double tone = math::halftoneIgnition(energy, threshold, h.screenParams[0],
                                               h.screenParams[1], h.screenParams[2]);
    const double widthTone = math::halftoneIgnition(ridge, threshold, h.screenParams[0],
                                                    h.screenParams[1], h.screenParams[2]);
    const double fillX =
        h.spotParams[2] + (1 - h.spotParams[2]) * std::pow(widthTone, h.spotParams[3]);
    const auto shape = [&](math::Vector2 p, double fill) {
        const math::Vector2 delta{p.x - cell.center.x, p.y - cell.center.y};
        const math::Vector2 q{delta.x * axis.x + delta.y * axis.y,
                              -delta.x * axis.y + delta.y * axis.x};
        if ((h.modeFlags & halftoneFlags::superellipse) != 0) {
            return math::superellipse(q, {halfX * fill, halfY}, h.spotParams[1]);
        }
        const double radius = std::min({static_cast<double>(h.spotParams[0]), halfX * fill, halfY});
        return math::roundedRect(q, {halfX * fill, halfY}, {radius, radius, radius, radius});
    };
    const auto areaFraction = [&](double fill) {
        if ((h.modeFlags & halftoneFlags::superellipse) != 0) {
            return 4 * halfX * fill * halfY * desc.params[0] / lattice.area();
        }
        return math::pillArea(2 * halfX * fill, 2 * halfY, h.spotParams[0]) / lattice.area();
    };
    const auto coverage = [&](double fill) {
        double exact = 0;
        if (eta > 0) {
            const double distance = shape(local, fill);
            if (std::abs(distance) >= reach) {
                exact = distance < 0 ? 1 : 0;
            } else if ((h.modeFlags & halftoneFlags::superellipse) == 0 || h.spotParams[1] == 2) {
                math::CoveragePolygon pixel;
                pixel.count = 4;
                constexpr std::array<math::Vector2, 4> corners{
                    {{-0.5, -0.5}, {0.5, -0.5}, {0.5, 0.5}, {-0.5, 0.5}}};
                for (std::size_t i = 0; i < 4; ++i) {
                    const auto c = corners[i];
                    const math::Vector2 q{
                        delta.x + transform.inverse[0] * c.x + transform.inverse[2] * c.y,
                        delta.y + transform.inverse[1] * c.x + transform.inverse[3] * c.y};
                    pixel.points[i] = {q.x * axis.x + q.y * axis.y, -q.x * axis.y + q.y * axis.x};
                }
                if ((h.modeFlags & halftoneFlags::superellipse) != 0) {
                    for (auto &p : pixel.points) {
                        p.x /= halfX * fill;
                        p.y /= halfY;
                    }
                    exact = math::coverageDisk(pixel, {}, 1) / math::coveragePolygonArea(pixel);
                } else {
                    exact =
                        math::roundedSpotPixelArea(pixel, {halfX * fill, halfY}, h.spotParams[0]) /
                        math::coveragePolygonArea(pixel);
                }
                exact = std::clamp(exact, 0.0, 1.0);
            } else {
                exact = detail::fieldCoverage(transform, local,
                                              [&](math::Vector2 p) { return shape(p, fill); });
            }
        }
        return eta * exact + (primary ? (1 - eta) * areaFraction(fill) : 0);
    };
    const double spot = coverage(fillX), grid = coverage(1);
    const auto emit = palette(frame, h.paletteRow, tone);
    return {grid * (0.0095 - 0.0033465358) + spot * (emit.r - 0.0095),
            grid * (0.0072 - 0.0036765073) + spot * (emit.g - 0.0072),
            grid * (0.0060 - 0.0043914420) + spot * (emit.b - 0.0060), 0};
}

math::PremultipliedColor shadeHalftone(const FrameView &frame, const PrimitiveDesc &desc,
                                       const TransformRecord &transform,
                                       math::Vector2 local) noexcept {
    HalftonePayload h;
    std::memcpy(&h, frame.payload.data() + desc.payloadOffset, sizeof(h));
    if (desc.auxHandle >= frame.textures.size()) {
        return {0, 0, 0, 0};
    }
    const math::HalftoneLattice lattice{{h.basis[0], h.basis[1]},
                                        {h.basis[2], h.basis[3]},
                                        {h.origin[0], h.origin[1]},
                                        (h.modeFlags & halftoneFlags::hexagonal) != 0
                                            ? math::HalftoneLatticeKind::hexagonal
                                            : math::HalftoneLatticeKind::brick};
    const auto physicalLength = [&](math::Vector2 v) {
        return std::hypot(transform.forward[0] * v.x + transform.forward[2] * v.y,
                          transform.forward[1] * v.x + transform.forward[3] * v.y);
    };
    const double eta =
        smooth(3.6, 7.2, std::min(physicalLength(lattice.b1), physicalLength(lattice.b2)));
    auto nearest = lattice.cell(local);
    if (lattice.kind == math::HalftoneLatticeKind::hexagonal) {
        const auto c = lattice.coordinates(local);
        const double z = -c.x - c.y;
        double x = std::round(c.x), y = std::round(c.y);
        const double rz = std::round(z);
        const double ex = std::abs(x - c.x), ey = std::abs(y - c.y), ez = std::abs(rz - z);
        if (ex > ey + 1e-5 && ex > ez + 1e-5) {
            x = -y - rz;
        } else if (ey > ez + 1e-5) {
            y = -x - rz;
        }
        nearest.x = static_cast<std::int32_t>(x);
        nearest.y = static_cast<std::int32_t>(y);
        nearest.center = {lattice.origin.x + x * lattice.b1.x + y * lattice.b2.x,
                          lattice.origin.y + x * lattice.b1.y + y * lattice.b2.y};
    }
    double r = 0.0033465358, g = 0.0036765073, b = 0.0043914420;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const math::Vector2 center{nearest.center.x + x * lattice.b1.x + y * lattice.b2.x,
                                       nearest.center.y + x * lattice.b1.y + y * lattice.b2.y};
            const math::HalftoneCell cell{nearest.x + x, nearest.y + y, center, local - center};
            const auto color = shadeHalftoneCell(frame, desc, transform, local, h, lattice, cell,
                                                 eta, x == 0 && y == 0);
            r += color.r;
            g += color.g;
            b += color.b;
        }
    }
    if ((h.modeFlags & halftoneFlags::cursor) != 0) {
        const double sx = physicalLength({1, 0});
        const double x = h.rect[0] + h.cursorParams[0] * (h.rect[2] - h.rect[0]);
        const double snapped = (std::floor(x * sx) + 0.5) / sx;
        double cursor = math::boxCoverage(std::abs(local.x - snapped) * sx - 0.75, {1, 0});
        const double cap = detail::fieldCoverage(transform, local, [&](math::Vector2 p) {
            return math::roundedTriangle(p, {snapped - 4 / sx, h.rect[1]},
                                         {snapped + 4 / sx, h.rect[1]},
                                         {snapped, h.rect[1] + 5 / sx}, 0.4 / sx);
        });
        cursor = std::max(cursor, cap);
        const int voices = std::clamp(static_cast<int>(h.cursorParams[3]), 1, 16);
        for (int v = 0; v < voices && voices > 1; ++v) {
            const double voiceX = snapped + (static_cast<double>(v) / (voices - 1) - 0.5) * 0.05 *
                                                (h.rect[2] - h.rect[0]);
            cursor = std::max(
                cursor, 0.18 * math::boxCoverage(std::abs(local.x - voiceX) * sx - 0.35, {1, 0}));
        }
        r += cursor * (0.686685 - r);
        g += cursor * (0.715694 - g);
        b += cursor * (0.760525 - b);
    }
    return {r, g, b, 1};
}
}
