#include "render/Wavetable3d.h"

#include "math/Coverage.h"
#include "render/PrimitiveShading.h"
#include "render/ShadingSupport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace prism::render {
namespace {
using math::Vector2;
float extraPosition(const Wavetable3dUniforms &u, std::uint32_t extra) noexcept {
    if (extra == 0) {
        return u.shape[0];
    }
    const float offset =
        u.voices > 1 ? 2 * static_cast<float>(extra - 1) / static_cast<float>(u.voices - 1) - 1 : 1;
    return std::clamp(u.shape[0] + u.shape[2] * offset, 0.0f, 1.0f);
}
struct Vertex {
    Vector2 p;
    double v = 0, distance = 0;
    Vector2 normal{};
    double focus = 0, depth = 0;
    unsigned mode = 0;
    bool active = false, ghost = false;
};
Vector2 unit(Vector2 v) noexcept {
    const double length = std::hypot(v.x, v.y);
    return length > 1e-12 ? v * (1 / length) : Vector2{1, 0};
}
Vector2 physical(const TransformRecord &t, Vector2 p) noexcept {
    const float x = static_cast<float>(p.x), y = static_cast<float>(p.y);
    return {t.forward[0] * x + t.forward[2] * y + t.forward[4],
            t.forward[1] * x + t.forward[3] * y + t.forward[5]};
}
Vector2 rasterProject(const Wavetable3dUniforms &u, double x, double y, double z,
                      double &depth) noexcept {
    const float fx = static_cast<float>(x), fy = static_cast<float>(y), fz = static_cast<float>(z);
    const float xr = u.camera[0] * fx + u.camera[1] * fz;
    const float zr = -u.camera[1] * fx + u.camera[0] * fz;
    const float yr = u.camera[2] * u.projection[1] * fy - u.camera[3] * zr;
    const float dz = u.camera[3] * u.projection[1] * fy + u.camera[2] * zr;
    depth = dz;
    const float scale = u.projection[2] / (1.0f - dz * u.projection[0]);
    return {(u.rect[0] + u.rect[2]) * 0.5f + scale * xr,
            (u.rect[1] + u.rect[3]) * 0.5f - scale * yr};
}
float rasterCubic(float a, float b, float c, float d, float t) noexcept {
    return b +
           0.5f * t *
               (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
}
float rasterSample(math::WavetableView table, const Wavetable3dUniforms &u, float theta,
                   float morph, int voice) noexcept {
    constexpr float turn = 6.2831853071795864769f;
    float offset = u.warp[3];
    if (voice >= 0) {
        const float spread =
            u.voices > 1
                ? 2.0f * static_cast<float>(voice) / static_cast<float>(u.voices - 1) - 1.0f
                : 1.0f;
        offset += spread * (u.warp[9] * u.warp[10] + u.warp[11]);
    }
    float turns = (theta + offset) / turn;
    turns -= std::floor(turns);
    if (u.warp[0] > 0) {
        const float peak = 1.0f - 0.5f * std::clamp(u.warp[0], 0.0f, 1.0f);
        turns = turns <= peak ? turns / peak : 1.0f - (turns - peak) / (1.0f - peak);
    }
    if (u.warp[1] != 0) {
        turns = std::pow(turns, std::exp2(u.warp[1]));
    }
    turns *= u.warp[2];
    const float phase = turns * turn;
    const auto frameSample = [&](int frame) {
        float phaseTurns = phase / turn;
        const float position =
            (phaseTurns - std::floor(phaseTurns)) * static_cast<float>(table.period);
        const int base = static_cast<int>(std::floor(position)),
                  size = static_cast<int>(table.period);
        const auto row =
            static_cast<std::size_t>(std::clamp(frame, 0, static_cast<int>(table.frames) - 1));
        const auto at = [&](int n) {
            return table.samples[row * table.period + static_cast<std::size_t>((n + size) % size)];
        };
        return rasterCubic(at(base - 1), at(base), at(base + 1), at(base + 2),
                           position - std::floor(position));
    };
    const float frame = std::clamp(morph, 0.0f, 1.0f) * static_cast<float>(table.frames - 1);
    const int base = static_cast<int>(std::floor(frame));
    float value = rasterCubic(frameSample(base - 1), frameSample(base), frameSample(base + 1),
                              frameSample(base + 2), frame - std::floor(frame));
    if (u.warp[4] > 0) {
        const float q = value * u.warp[4], f = q - std::floor(q);
        value = (std::floor(q) + 0.5f +
                 0.5f * std::tanh(u.warp[5] * (f - 0.5f)) / std::tanh(u.warp[5] * 0.5f)) /
                u.warp[4];
    }
    value *= 1.0f - u.warp[6] + u.warp[6] * std::cos(u.warp[7] * theta + u.warp[8]);
    return std::clamp(value, -1.0f, 1.0f);
}
Rgba palette(const FrameView &frame, std::uint32_t row, double energy) noexcept {
    const auto offset = static_cast<std::size_t>(row) * 1024;
    if (offset + 1024 > frame.gradientRows.size()) {
        return {};
    }
    const double p = std::clamp(energy, 0.0, 1.0) * 255;
    const auto a = static_cast<std::size_t>(p), b = std::min(a + 1, std::size_t{255});
    const double t = p - static_cast<double>(a);
    Rgba result{};
    for (std::size_t c = 0; c < 4; ++c) {
        result[c] = static_cast<float>(frame.gradientRows[offset + a * 4 + c] * (1 - t) +
                                       frame.gradientRows[offset + b * 4 + c] * t);
    }
    return result;
}
Rgba fragment(const FrameView &frame, const PrimitiveDesc &desc, const Wavetable3dUniforms &u,
              const Vertex &v, Vector2 pixel) noexcept {
    const auto local = detail::toLocal(frame.transforms[desc.transformId], pixel.x, pixel.y);
    if (local.x < u.rect[0] || local.x > u.rect[2] || local.y < u.rect[1] || local.y > u.rect[3]) {
        return {};
    }
    Rgba result{};
    const double depth = std::exp(-0.45 * std::clamp(0.5 * (u.projection[3] - v.depth), 0.0, 1.0));
    if (v.mode == 0) {
        const double vertical = std::clamp(v.v, 0.0, 1.0);
        const double alpha = (v.active ? 0.34 : 0.17) * v.focus * depth *
                             std::exp(-2.4 * vertical) * std::pow(1 - vertical, 0.45) *
                             (v.ghost ? 0.35 : 1);
        result = palette(frame, desc.gradientOffset, 0.32 + 0.36 * v.focus * (1 - vertical));
        for (std::size_t c = 0; c < 3; ++c) {
            result[c] *= static_cast<float>(alpha);
        }
        result[3] = static_cast<float>(alpha);
    } else {
        const double width = v.mode == 2 ? 1 : v.active ? 1.9 : 1.1;
        const double core = math::boxCoverage(std::abs(v.distance) - width * 0.5, v.normal);
        if (v.mode == 2) {
            const double alpha = 0.55 * core;
            result = {static_cast<float>(0.0273209 * alpha), static_cast<float>(0.0318960 * alpha),
                      static_cast<float>(0.0423114 * alpha), static_cast<float>(alpha)};
        } else if (v.active) {
            const double halo = std::exp(-v.distance * v.distance / (2 * 2.6 * 2.6));
            const auto hot = palette(frame, desc.gradientOffset, 0.98);
            const auto warm = palette(frame, desc.gradientOffset, 0.58);
            for (std::size_t c = 0; c < 3; ++c) {
                result[c] = static_cast<float>((hot[c] * core + 0.42 * warm[c] * halo) * depth);
            }
            result[3] = static_cast<float>(core * depth);
        } else {
            const double alpha = 0.75 * v.focus * depth * core * (v.ghost ? 0.35 : 1);
            result = palette(frame, desc.gradientOffset, 0.30 + 0.40 * v.focus);
            for (std::size_t c = 0; c < 3; ++c) {
                result[c] *= static_cast<float>(alpha);
            }
            result[3] = static_cast<float>(alpha);
        }
    }
    const double clip = clipCoverage(frame, desc.clipId, pixel.x, pixel.y);
    for (float &c : result) {
        c *= static_cast<float>(clip);
    }
    return result;
}
void over(Rgba &destination, const Rgba &source) noexcept {
    for (std::size_t c = 0; c < 4; ++c) {
        destination[c] = source[c] + (1 - source[3]) * destination[c];
    }
}
bool topLeft(Vector2 a, Vector2 b) noexcept { return b.y < a.y || (b.y == a.y && b.x > a.x); }
struct RasterTriangle {
    const Vertex *a, *b, *c;
    Vector2 bc, ca, ab;
    Vector2 coverageA, coverageB, coverageC, coverageBC, coverageCA, coverageAB;
    double inverseArea;
    bool includeX, includeY, includeZ;
    RasterTriangle(const Vertex &first, const Vertex &second, const Vertex &third) noexcept
        : a(&first), b(&second), c(&third) {
        const auto snap = [](Vector2 p) {
            return Vector2{std::round(p.x * 256) / 256, std::round(p.y * 256) / 256};
        };
        coverageA = snap(a->p);
        coverageB = snap(b->p);
        coverageC = snap(c->p);
        double area = cross(coverageB - coverageA, coverageC - coverageA);
        if (area < 0) {
            std::swap(b, c);
            std::swap(coverageB, coverageC);
            area = -area;
        }
        const double interpolationArea = cross(b->p - a->p, c->p - a->p);
        inverseArea = area > 0 && interpolationArea != 0 ? 1 / interpolationArea : 0;
        bc = c->p - b->p;
        ca = a->p - c->p;
        ab = b->p - a->p;
        coverageBC = coverageC - coverageB;
        coverageCA = coverageA - coverageC;
        coverageAB = coverageB - coverageA;
        includeX = topLeft(coverageB, coverageC);
        includeY = topLeft(coverageC, coverageA);
        includeZ = topLeft(coverageA, coverageB);
    }
    [[nodiscard]] bool interpolate(Vector2 p, Vertex &result) const noexcept {
        if (inverseArea == 0) {
            return false;
        }
        const double x = cross(coverageBC, p - coverageB), y = cross(coverageCA, p - coverageC),
                     z = cross(coverageAB, p - coverageA);
        if (x < 0 || (x == 0 && !includeX) || y < 0 || (y == 0 && !includeY) || z < 0 ||
            (z == 0 && !includeZ)) {
            return false;
        }
        const double wa = cross(bc, p - b->p) * inverseArea, wb = cross(ca, p - c->p) * inverseArea,
                     wc = cross(ab, p - a->p) * inverseArea;
        result.mode = a->mode;
        result.active = a->active;
        result.ghost = a->ghost;
        result.v = wa * a->v + wb * b->v + wc * c->v;
        result.distance = wa * a->distance + wb * b->distance + wc * c->distance;
        result.normal = a->normal * wa + b->normal * wb + c->normal * wc;
        result.focus = wa * a->focus + wb * b->focus + wc * c->focus;
        result.depth = wa * a->depth + wb * b->depth + wc * c->depth;
        return true;
    }
};
template <class Emit>
void triangles(const FrameView &frame, const PrimitiveDesc &desc, const Wavetable3dUniforms &u,
               Emit &&emit) noexcept {
    if (desc.auxHandle >= frame.textures.size()) {
        return;
    }
    const auto &texture = frame.textures[desc.auxHandle];
    if (texture.channels != 1 || texture.texels.empty()) {
        return;
    }
    const math::WavetableView table{texture.texels, texture.height, texture.width};
    const auto &transform = frame.transforms[desc.transformId];
    const auto quad = [&](const Vertex &a, const Vertex &b, const Vertex &c, const Vertex &d) {
        emit(a, b, c);
        emit(b, d, c);
    };
    const auto wire = [&](bool front) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            for (unsigned other = 0; other < 4; ++other) {
                double q[3]{}, r[3]{};
                unsigned bit = 0;
                for (unsigned k = 0; k < 3; ++k) {
                    if (k == axis) {
                        q[k] = -1;
                        r[k] = 1;
                    } else {
                        q[k] = r[k] = (other & (1u << bit++)) != 0 ? 1 : -1;
                    }
                }
                double za, zb;
                const auto a = physical(transform, rasterProject(u, q[0], q[1], q[2], za));
                const auto b = physical(transform, rasterProject(u, r[0], r[1], r[2], zb));
                if (((za + zb) >= 0) != front) {
                    continue;
                }
                const auto tangent = unit(b - a);
                const Vector2 normal{-tangent.y, tangent.x};
                Vertex v0, v1, v2, v3;
                v0.p = a - normal * 1.5;
                v1.p = a + normal * 1.5;
                v2.p = b - normal * 1.5;
                v3.p = b + normal * 1.5;
                v0.distance = v2.distance = -1.5;
                v1.distance = v3.distance = 1.5;
                for (Vertex *v : {&v0, &v1, &v2, &v3}) {
                    v->normal = normal;
                    v->mode = 2;
                }
                quad(v0, v1, v2, v3);
            }
        }
    };
    wire(false);
    const auto phases = std::min(u.phases, 256u);
    for (std::uint32_t slice = 0; slice < u.slices + 1 + u.voices; ++slice) {
        bool active;
        int voice;
        const double morph = wavetableSlice(u, slice, active, voice);
        const double focus =
            u.shape[3] +
            (1 - u.shape[3]) * std::exp(-std::pow(morph - u.shape[0], 2) / (2 * 0.14 * 0.14));
        std::array<Vertex, 256> crest{}, base{}, negative{}, positive{};
        for (std::uint32_t i = 0; i < phases; ++i) {
            const float phase =
                6.2831853071795864769f * static_cast<float>(i) / static_cast<float>(phases - 1);
            const float x = -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(phases - 1);
            const float z = 2.0f * static_cast<float>(morph) - 1.0f;
            const float value = rasterSample(table, u, phase, static_cast<float>(morph), voice);
            double depth;
            crest[i].p = physical(transform, rasterProject(u, x, value, z, depth));
            crest[i].depth = depth;
            base[i].p = physical(transform, rasterProject(u, x, u.shape[1], z, depth));
            base[i].depth = depth;
            base[i].v = 1;
            for (Vertex *v : {&crest[i], &base[i]}) {
                v->focus = focus;
                v->active = active;
                v->ghost = voice >= 0;
            }
        }
        for (std::uint32_t i = 0; i + 1 < phases; ++i) {
            quad(crest[i], base[i], crest[i + 1], base[i + 1]);
        }
        const double radius = active ? 0.95 + 3 * 2.6 : 0.55 + 1;
        for (std::uint32_t i = 0; i < phases; ++i) {
            const auto before = unit(crest[i].p - crest[i == 0 ? 0 : i - 1].p);
            const auto after = unit(crest[i + 1 < phases ? i + 1 : i].p - crest[i].p);
            Vector2 tangent = i == 0 ? after : i + 1 == phases ? before : unit(before + after);
            const Vector2 normal{-tangent.y, tangent.x};
            const auto segment = i + 1 == phases ? before : after;
            const Vector2 segmentNormal{-segment.y, segment.x};
            const double extent =
                radius / std::max(0.25, std::abs(math::dot(normal, segmentNormal)));
            negative[i] = positive[i] = crest[i];
            negative[i].p = crest[i].p - normal * extent;
            positive[i].p = crest[i].p + normal * extent;
            negative[i].distance = -radius;
            positive[i].distance = radius;
            negative[i].normal = positive[i].normal = normal;
            negative[i].mode = positive[i].mode = 1;
        }
        for (std::uint32_t i = 0; i + 1 < phases; ++i) {
            quad(negative[i], positive[i], negative[i + 1], positive[i + 1]);
        }
    }
    wire(true);
}
}
float wavetableSlice(const Wavetable3dUniforms &u, std::uint32_t index, bool &active,
                     int &voice) noexcept {
    const bool descending = (u.flags & 1) != 0;
    std::array<std::uint32_t, 5> ranks{};
    for (std::uint32_t e = 0; e <= u.voices; ++e) {
        const float p = extraPosition(u, e);
        const float distance = descending ? 1 - p : p;
        std::uint32_t rank =
            static_cast<std::uint32_t>(std::ceil(distance * static_cast<float>(u.slices - 1)));
        for (std::uint32_t j = 0; j <= u.voices; ++j) {
            const float q = extraPosition(u, j);
            rank += (descending ? q > p : q < p) || (q == p && j < e);
        }
        ranks[e] = rank;
        if (rank == index) {
            active = e == 0;
            voice = static_cast<int>(e) - 1;
            return p;
        }
    }
    std::uint32_t structural = index;
    for (std::uint32_t e = 0; e <= u.voices; ++e) {
        structural -= ranks[e] < index;
    }
    active = false;
    voice = -1;
    const float position = static_cast<float>(structural) / static_cast<float>(u.slices - 1);
    return descending ? 1 - position : position;
}
void rasterWavetable3d(const FrameView &frame, const PrimitiveDesc &desc, FloatImage &image,
                       std::uint32_t width, std::uint32_t height) {
    image.resize(width, height);
    Wavetable3dUniforms u;
    std::memcpy(&u, frame.payload.data() + desc.payloadOffset, sizeof(u));
    triangles(frame, desc, u, [&](const Vertex &a, const Vertex &b, const Vertex &c) {
        const RasterTriangle triangle(a, b, c);
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.p.x, b.p.x, c.p.x}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.p.y, b.p.y, c.p.y}))));
        const int x1 = std::min(static_cast<int>(width),
                                static_cast<int>(std::ceil(std::max({a.p.x, b.p.x, c.p.x}))));
        const int y1 = std::min(static_cast<int>(height),
                                static_cast<int>(std::ceil(std::max({a.p.y, b.p.y, c.p.y}))));
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const Vector2 pixel{x + 0.5, y + 0.5};
                Vertex v;
                if (triangle.interpolate(pixel, v)) {
                    over(image.at(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)),
                         fragment(frame, desc, u, v, pixel));
                }
            }
        }
    });
}
math::PremultipliedColor shadeWavetable3d(const FrameView &frame, const PrimitiveDesc &desc,
                                          double x, double y) noexcept {
    Wavetable3dUniforms u;
    std::memcpy(&u, frame.payload.data() + desc.payloadOffset, sizeof(u));
    Rgba color{};
    triangles(frame, desc, u, [&](const Vertex &a, const Vertex &b, const Vertex &c) {
        Vertex v;
        if (RasterTriangle(a, b, c).interpolate({x, y}, v)) {
            over(color, fragment(frame, desc, u, v, {x, y}));
        }
    });
    return {color[0], color[1], color[2], color[3]};
}
}
