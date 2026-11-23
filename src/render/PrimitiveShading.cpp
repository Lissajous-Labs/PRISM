#include "render/PrimitiveShading.h"

#include "math/Pchip.h"
#include "math/Sdf.h"
#include "math/ShadowMath.h"
#include "math/StemDarkening.h"
#include "render/FormantCurve.h"
#include "render/GradientParameter.h"
#include "render/HalftoneField.h"
#include "render/ShadingSupport.h"
#include "render/SlugCoverage.h"
#include "render/TextureSampling.h"
#include "render/Wavetable3d.h"

#include <algorithm>
#include <cmath>

namespace prism::render {
namespace {
using math::Vector2;

Vector2 point(const FrameView &frame, std::uint32_t offset) noexcept {
    return {frame.payload[offset], frame.payload[offset + 1]};
}

struct LocalBox {
    Vector2 center;
    Vector2 half;
};

LocalBox box(const FrameView &frame, std::uint32_t offset) noexcept {
    const Vector2 low = point(frame, offset);
    const Vector2 high = point(frame, offset + 2);
    return {(low + high) * 0.5, (high - low) * 0.5};
}

LocalBox rectBox(const PrimitiveDesc &desc) noexcept {
    const auto rect = desc.shapeRect();
    const Vector2 low{rect[0], rect[1]};
    const Vector2 high{rect[2], rect[3]};
    return {(low + high) * 0.5, (high - low) * 0.5};
}

math::CornerRadii radii(const PrimitiveDesc &desc) noexcept {
    return {desc.params[0], desc.params[1], desc.params[2], desc.params[3]};
}

math::PremultipliedColor fillColor(const FrameView &frame, const PrimitiveDesc &desc,
                                   Vector2 local) noexcept {
    if (!desc.has(flags::gradient)) {
        return detail::decodeColor(desc.color);
    }
    const auto shape = static_cast<GradientShape>((desc.kindFlags & flags::gradientShapeMask) >>
                                                  flags::gradientShapeShift);
    const double t = gradientParameter(shape, frame.payload.data() + desc.gradientOffset, local);
    const double position = t * (gradientRowTexels - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = std::min<std::size_t>(lower + 1, gradientRowTexels - 1);
    const double weight = position - static_cast<double>(lower);
    const std::size_t row = static_cast<std::size_t>(desc.auxHandle) * gradientRowTexels * 4;
    const auto channel = [&](std::size_t c) {
        return frame.gradientRows[row + lower * 4 + c] * (1 - weight) +
               frame.gradientRows[row + upper * 4 + c] * weight;
    };
    const double alpha = static_cast<double>((desc.color >> 24) & 0xFFu) / 255;
    return detail::scale({channel(0), channel(1), channel(2), channel(3)}, alpha);
}

ShadeResult solid(math::PremultipliedColor color) noexcept { return {color, {0, 0, 0}, false}; }

ShadeResult shadeRoundedRect(const FrameView &frame, const PrimitiveDesc &desc,
                             const TransformRecord &transform, Vector2 local) noexcept {
    const LocalBox shape = rectBox(desc);
    const auto coverage = [&](Vector2 center, Vector2 half, math::CornerRadii corners) {
        const Vector2 p = local - center;
        return detail::gradientCoverage(transform, math::roundedRect(p, half, corners),
                                        math::roundedRectGradient(p, half, corners));
    };
    const double outerCoverage = coverage(shape.center, shape.half, radii(desc));
    const auto fill = fillColor(frame, desc, local);
    if (!desc.has(flags::border)) {
        return solid(detail::scale(fill, outerCoverage));
    }
    const std::uint32_t widths = desc.payloadOffset;
    const auto inner = math::innerBorder(shape.half, radii(desc),
                                         {frame.payload[widths], frame.payload[widths + 1],
                                          frame.payload[widths + 2], frame.payload[widths + 3]});
    const double innerCoverage = coverage(shape.center + inner.center, inner.half, inner.radii);
    const auto weights = math::borderWeights(outerCoverage, innerCoverage);
    const auto border = detail::decodeColor(desc.borderColor);
    const auto f = detail::scale(fill, weights.fill);
    const auto b = detail::scale(border, weights.border);
    return solid({f.r + b.r, f.g + b.g, f.b + b.b, f.a + b.a});
}

template <class Field>
ShadeResult shadeField(const FrameView &frame, const PrimitiveDesc &desc,
                       const TransformRecord &transform, Vector2 local, Field &&field) noexcept {
    return solid(detail::scale(fillColor(frame, desc, local),
                               detail::fieldCoverage(transform, local, field)));
}

ShadeResult shadeShadow(const PrimitiveDesc &desc, const TransformRecord &transform,
                        Vector2 local) noexcept {
    const LocalBox shape = rectBox(desc);
    const double sigma = desc.params[0];
    const double radius = desc.params[1];
    const double blurred = math::roundedBoxShadow(local - shape.center, shape.half, radius, sigma);
    double alpha = blurred;
    if (desc.has(flags::inset)) {
        const math::CornerRadii corners{radius, radius, radius, radius};
        const Vector2 p = local - shape.center;
        alpha = detail::gradientCoverage(transform, math::roundedRect(p, shape.half, corners),
                                         math::roundedRectGradient(p, shape.half, corners)) *
                (1 - blurred);
    }
    return solid(detail::scale(detail::decodeColor(desc.color), alpha));
}

Vector2 textureCoordinate(const FrameView &frame, const PrimitiveDesc &desc,
                          Vector2 local) noexcept {
    const double u = (local.x - desc.bounds[0]) / (desc.bounds[2] - desc.bounds[0]);
    const double v = (local.y - desc.bounds[1]) / (desc.bounds[3] - desc.bounds[1]);
    const std::uint32_t uv = desc.payloadOffset;
    return {frame.payload[uv] + u * (frame.payload[uv + 2] - frame.payload[uv]),
            frame.payload[uv + 1] + v * (frame.payload[uv + 3] - frame.payload[uv + 1])};
}

ShadeResult shadeTexture(const FrameView &frame, const PrimitiveDesc &desc,
                         const TransformRecord &transform, Vector2 local) noexcept {
    if (desc.auxHandle >= frame.textures.size()) {
        return solid({0, 0, 0, 0});
    }
    const TextureView &texture = frame.textures[desc.auxHandle];
    const bool texelUnits = desc.params[3] > 0;
    const double scaleU = texelUnits ? 1.0 : texture.width;
    const double scaleV = texelUnits ? 1.0 : texture.height;
    const Vector2 raw = textureCoordinate(frame, desc, local);
    const Vector2 uv = texelUnits && texture.width != 0 && texture.height != 0
                           ? Vector2{raw.x / texture.width, raw.y / texture.height}
                           : raw;
    const Texel texel = sampleFiltered(texture, uv.x, uv.y);
    const auto color = detail::decodeColor(desc.color);
    const float luminance =
        math::foregroundLuminance(static_cast<float>(color.r), static_cast<float>(color.g),
                                  static_cast<float>(color.b), static_cast<float>(color.a));
    const float bias = desc.params[1] > 0
                           ? math::polarityStemBias(desc.params[1], desc.params[2], luminance)
                           : 0.0f;
    switch (desc.kind()) {
    case PrimitiveKind::glyphAlpha:
        return solid(
            detail::scale(color, math::shiftedCoverage(static_cast<float>(texel[0]), bias)));
    case PrimitiveKind::glyphLcd: {
        const double r = math::shiftedCoverage(static_cast<float>(texel[0]), bias);
        const double g = math::shiftedCoverage(static_cast<float>(texel[1]), bias);
        const double b = math::shiftedCoverage(static_cast<float>(texel[2]), bias);
        ShadeResult result{{color.r * r, color.g * g, color.b * b, color.a * (r + g + b) / 3},
                           {color.a * r, color.a * g, color.a * b},
                           true};
        return result;
    }
    case PrimitiveKind::glyphMtsdf: {
        const double median = std::max(std::min(texel[0], texel[1]),
                                       std::min(std::max(texel[0], texel[1]), texel[2]));
        const std::uint32_t uvOffset = desc.payloadOffset;
        const double su = (frame.payload[uvOffset + 2] - frame.payload[uvOffset]) * scaleU /
                          (desc.bounds[2] - desc.bounds[0]);
        const double sv = (frame.payload[uvOffset + 3] - frame.payload[uvOffset + 1]) * scaleV /
                          (desc.bounds[3] - desc.bounds[1]);
        const double j00 = su * transform.inverse[0];
        const double j01 = su * transform.inverse[2];
        const double j10 = sv * transform.inverse[1];
        const double j11 = sv * transform.inverse[3];
        const double footprint =
            std::sqrt(std::max(0.5 * (j00 * j00 + j01 * j01 + j10 * j10 + j11 * j11), 1e-16));
        const double screenRange = std::max(desc.params[0] / footprint, 1.0);
        const double distance = (median - 0.5) * screenRange + bias;
        return solid(detail::scale(color, std::clamp(distance + 0.5, 0.0, 1.0)));
    }
    default:
        return solid(
            {texel[0] * color.a, texel[1] * color.a, texel[2] * color.a, texel[3] * color.a});
    }
}

ShadeResult shadePlot(const FrameView &frame, const PrimitiveDesc &desc,
                      const TransformRecord &transform, Vector2 local) noexcept {
    if (desc.auxHandle >= frame.textures.size()) {
        return solid({0, 0, 0, 0});
    }
    const TextureView &strip = frame.textures[desc.auxHandle];
    const LocalBox shape = box(frame, desc.payloadOffset);
    const double columns = desc.params[1];
    const double halfThickness = desc.params[0] / 2;
    const double position =
        std::clamp((local.x - (shape.center.x - shape.half.x)) / (2 * shape.half.x), 0.0, 1.0);
    const auto count = static_cast<std::int64_t>(columns);
    const auto raw = static_cast<std::int64_t>(
        std::floor(std::min(position * columns, columns - 1) + desc.params[2]));
    const auto column = (raw % count + count) % count;
    const Texel band = fetchTexel(strip, column, 0);
    const double base = shape.center.y + shape.half.y;
    const bool fill = frame.payload[desc.payloadOffset + 4] > 0;
    const double top = base - (fill ? band[0] : band[1]) * 2 * shape.half.y;
    const double bottom = fill ? base : base - band[0] * 2 * shape.half.y;
    const bool above = top - local.y > local.y - bottom;
    const double distance = (above ? top - local.y : local.y - bottom) - halfThickness;
    auto result = solid(
        detail::scale(fillColor(frame, desc, local),
                      detail::gradientCoverage(transform, distance, {0, above ? -1.0 : 1.0})));
    if (desc.params[3] > 0) {
        const double u = (local.x - shape.center.x + shape.half.x) / (2 * shape.half.x);
        const auto smooth = [&](double value) {
            const double t = std::clamp(value / desc.params[3], 0.0, 1.0);
            return t * t * (3 - 2 * t);
        };
        result.color = detail::scale(result.color, smooth(u) * smooth(1 - u));
    }
    return result;
}
}

double clipCoverage(const FrameView &frame, std::uint16_t clipId, double x, double y) noexcept {
    double coverage = 1;
    while (clipId != noClip && clipId < frame.clips.size() && coverage > 0) {
        const ClipRecord &clip = frame.clips[clipId];
        const TransformRecord &transform = frame.transforms[clip.transformId];
        const Vector2 local = detail::toLocal(transform, x, y);
        const Vector2 center{(clip.bounds[0] + clip.bounds[2]) / 2.0,
                             (clip.bounds[1] + clip.bounds[3]) / 2.0};
        const Vector2 half{(clip.bounds[2] - clip.bounds[0]) / 2.0,
                           (clip.bounds[3] - clip.bounds[1]) / 2.0};
        const math::CornerRadii corner{clip.radii[0], clip.radii[1], clip.radii[2], clip.radii[3]};
        const auto field = [&](Vector2 p) {
            const Vector2 q = p - center;
            switch (clip.kind) {
            case ClipKind::rect:
                return math::box(q, half);
            case ClipKind::roundedRect:
                return math::roundedRect(q, half, corner);
            case ClipKind::ellipse: {
                const double k = math::length({q.x / half.x, q.y / half.y});
                return (k - 1) * std::min(half.x, half.y);
            }
            case ClipKind::none:
                return -1.0;
            }
            return -1.0;
        };
        coverage *= detail::fieldCoverage(transform, local, field);
        clipId = clip.parent;
    }
    return coverage;
}

ShadeResult shadePrimitive(const FrameView &frame, const PrimitiveDesc &desc, double x,
                           double y) noexcept {
    const TransformRecord &transform = frame.transforms[desc.transformId];
    const Vector2 local = detail::toLocal(transform, x, y);
    if (local.x < desc.bounds[0] || local.x > desc.bounds[2] || local.y < desc.bounds[1] ||
        local.y > desc.bounds[3]) {
        return solid({0, 0, 0, 0});
    }
    ShadeResult result = solid({0, 0, 0, 0});
    switch (desc.kind()) {
    case PrimitiveKind::roundedRect:
        result = shadeRoundedRect(frame, desc, transform, local);
        break;
    case PrimitiveKind::squircle: {
        const LocalBox shape = rectBox(desc);
        const auto sample = math::sampleCornerSquircle(local - shape.center, shape.half,
                                                       desc.params[0], desc.params[1]);
        result = solid(
            detail::scale(fillColor(frame, desc, local),
                          detail::gradientCoverage(transform, sample.distance, sample.gradient)));
        break;
    }
    case PrimitiveKind::circleArc: {
        const math::Arc arc{
            point(frame, desc.payloadOffset),
            desc.params[0],
            desc.params[1],
            desc.params[2],
            desc.params[3],
            static_cast<math::ArcCap>((desc.kindFlags & flags::capMask) >> flags::capShift)};
        const auto sample = math::sampleArc(local, arc);
        result = solid(
            detail::scale(fillColor(frame, desc, local),
                          detail::gradientCoverage(transform, sample.distance, sample.gradient)));
        break;
    }
    case PrimitiveKind::lineCapsule:
        result = shadeField(frame, desc, transform, local, [&](Vector2 p) {
            return math::capsule(p, point(frame, desc.payloadOffset),
                                 point(frame, desc.payloadOffset + 2), desc.params[0]);
        });
        break;
    case PrimitiveKind::quadBezier:
        result = shadeField(frame, desc, transform, local, [&](Vector2 p) {
            return math::quadraticBezier(p, point(frame, desc.payloadOffset),
                                         point(frame, desc.payloadOffset + 2),
                                         point(frame, desc.payloadOffset + 4)) -
                   desc.params[0];
        });
        break;
    case PrimitiveKind::triangle:
        result = shadeField(frame, desc, transform, local, [&](Vector2 p) {
            return math::roundedTriangle(p, point(frame, desc.payloadOffset),
                                         point(frame, desc.payloadOffset + 2),
                                         point(frame, desc.payloadOffset + 4), desc.params[0]);
        });
        break;
    case PrimitiveKind::boxShadow:
        result = shadeShadow(desc, transform, local);
        break;
    case PrimitiveKind::glyphAlpha:
    case PrimitiveKind::glyphLcd:
    case PrimitiveKind::glyphMtsdf:
    case PrimitiveKind::texturedQuad:
        result = shadeTexture(frame, desc, transform, local);
        break;
    case PrimitiveKind::plotStrip:
        result = shadePlot(frame, desc, transform, local);
        break;
    case PrimitiveKind::halftoneField:
        result = solid(shadeHalftone(frame, desc, transform, local));
        break;
    case PrimitiveKind::wavetable3d:
        return solid(shadeWavetable3d(frame, desc, x, y));
    case PrimitiveKind::formantCurve:
        result = solid(shadeFormantCurve(frame, desc, local));
        break;
    case PrimitiveKind::glyphSlug: {
        const std::uint32_t at = desc.payloadOffset;
        const double emX0 = frame.payload[at];
        const double emY0 = frame.payload[at + 1];
        const double emX1 = frame.payload[at + 2];
        const double emY1 = frame.payload[at + 3];
        const double localPerEmX = (desc.bounds[2] - desc.bounds[0]) / (emX1 - emX0);
        const double localPerEmY = (desc.bounds[3] - desc.bounds[1]) / (emY1 - emY0);
        const Vector2 em{emX0 + (local.x - desc.bounds[0]) / localPerEmX,
                         emY0 + (local.y - desc.bounds[1]) / localPerEmY};
        const double pixelsPerEmX =
            std::hypot(transform.forward[0], transform.forward[1]) * localPerEmX;
        const double pixelsPerEmY =
            std::hypot(transform.forward[2], transform.forward[3]) * localPerEmY;
        const auto blob = frame.payload.subspan(at + 4);
        result = solid(detail::scale(detail::decodeColor(desc.color),
                                     slugCoverage(blob, em, pixelsPerEmX, pixelsPerEmY)));
        break;
    }
    case PrimitiveKind::spectrumCurve: {
        const std::uint32_t at = desc.payloadOffset;
        const auto count = static_cast<std::uint32_t>(desc.params[1]);
        const double x0 = frame.payload[at];
        const double top = frame.payload[at + 1];
        const double x1 = frame.payload[at + 2];
        const double bottom = frame.payload[at + 3];
        const double spacing = (x1 - x0) / (count - 1);
        const auto segment = static_cast<std::uint32_t>(
            std::clamp(std::floor((local.x - x0) / spacing), 0.0, static_cast<double>(count - 2)));
        const double t =
            std::clamp((local.x - x0 - segment * spacing) / std::max(spacing, 1e-5), 0.0, 1.0);
        const double y0 = frame.payload[at + 4 + segment];
        const double y1 = frame.payload[at + 5 + segment];
        const double m0 = frame.payload[at + 4 + count + segment];
        const double m1 = frame.payload[at + 5 + count + segment];
        const double s = math::pchipEvaluate(y0, y1, m0, m1, spacing, t);
        const double ds = math::pchipDerivative(y0, y1, m0, m1, spacing, t);
        const double signedDistance = (s - local.y) / std::sqrt(1 + ds * ds);
        const double stroke =
            std::clamp(0.5 * desc.params[0] - std::abs(signedDistance) + 0.5, 0.0, 1.0);
        const double fillMask = std::clamp(0.5 - signedDistance, 0.0, 1.0);
        const double depth = std::clamp((local.y - s) / std::max(bottom - top, 1e-5), 0.0, 1.0);
        const double fill = fillMask * (0.28 + (0.02 - 0.28) * depth);
        result =
            solid(detail::scale(detail::decodeColor(desc.color), stroke + fill * (1 - stroke)));
        break;
    }
    }
    if (desc.clipId != noClip) {
        const double clip = clipCoverage(frame, desc.clipId, x, y);
        result.color = detail::scale(result.color, clip);
        for (double &channel : result.coverage) {
            channel *= clip;
        }
    }
    return result;
}
}
