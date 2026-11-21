#include "render/PrimitiveShading.h"

#include "math/Sdf.h"
#include "math/ShadowMath.h"
#include "render/GradientParameter.h"
#include "render/ShadingSupport.h"
#include "render/TextureSampling.h"

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
    const LocalBox shape = box(frame, desc.payloadOffset);
    const auto outer = [&](Vector2 p) {
        return math::roundedRect(p - shape.center, shape.half, radii(desc));
    };
    const double outerCoverage = detail::fieldCoverage(transform, local, outer);
    const auto fill = fillColor(frame, desc, local);
    if (!desc.has(flags::border)) {
        return solid(detail::scale(fill, outerCoverage));
    }
    const std::uint32_t widths = desc.payloadOffset + 4;
    const auto inner = math::innerBorder(shape.half, radii(desc),
                                         {frame.payload[widths], frame.payload[widths + 1],
                                          frame.payload[widths + 2], frame.payload[widths + 3]});
    const auto innerField = [&](Vector2 p) {
        return math::roundedRect(p - shape.center - inner.center, inner.half, inner.radii);
    };
    const double innerCoverage = detail::fieldCoverage(transform, local, innerField);
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

ShadeResult shadeShadow(const FrameView &frame, const PrimitiveDesc &desc,
                        const TransformRecord &transform, Vector2 local) noexcept {
    const LocalBox shape = box(frame, desc.payloadOffset);
    const double sigma = desc.params[0];
    const double radius = desc.params[1];
    const double blurred = math::roundedBoxShadow(local - shape.center, shape.half, radius, sigma);
    double alpha = blurred;
    if (desc.has(flags::inset)) {
        const math::CornerRadii corners{radius, radius, radius, radius};
        const auto inside = [&](Vector2 p) {
            return math::roundedRect(p - shape.center, shape.half, corners);
        };
        alpha = detail::fieldCoverage(transform, local, inside) * (1 - blurred);
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
    const Vector2 uv = textureCoordinate(frame, desc, local);
    const Texel texel = sampleBilinear(texture, uv.x, uv.y);
    const auto color = detail::decodeColor(desc.color);
    switch (desc.kind()) {
    case PrimitiveKind::glyphAlpha:
        return solid(detail::scale(color, texel[0]));
    case PrimitiveKind::glyphLcd: {
        ShadeResult result{{color.r * texel[0], color.g * texel[1], color.b * texel[2],
                            color.a * (texel[0] + texel[1] + texel[2]) / 3},
                           {color.a * texel[0], color.a * texel[1], color.a * texel[2]},
                           true};
        return result;
    }
    case PrimitiveKind::glyphMtsdf: {
        const double median = std::max(std::min(texel[0], texel[1]),
                                       std::min(std::max(texel[0], texel[1]), texel[2]));
        const double quadWidth = std::hypot(transform.forward[0], transform.forward[1]) *
                                 (desc.bounds[2] - desc.bounds[0]);
        const double texelsAcross =
            std::abs(frame.payload[desc.payloadOffset + 2] - frame.payload[desc.payloadOffset]) *
            texture.width;
        const double screenRange = desc.params[0] * quadWidth / std::max(texelsAcross, 1e-9);
        return solid(detail::scale(color, math::boxCoverage((0.5 - median) * screenRange, {1, 0})));
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
    const auto field = [&](Vector2 p) {
        const double u =
            std::clamp((p.x - (shape.center.x - shape.half.x)) / (2 * shape.half.x), 0.0, 1.0);
        const auto column = static_cast<std::int64_t>(std::min(u * columns, columns - 1));
        const Texel band = fetchTexel(strip, column, 0);
        const double top = shape.center.y + shape.half.y - band[1] * 2 * shape.half.y;
        const double bottom = shape.center.y + shape.half.y - band[0] * 2 * shape.half.y;
        const double outside = std::max(top - p.y, p.y - bottom);
        return outside - halfThickness;
    };
    return shadeField(frame, desc, transform, local, field);
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
        const LocalBox shape = box(frame, desc.payloadOffset);
        result = shadeField(frame, desc, transform, local, [&](Vector2 p) {
            return math::superellipse(p - shape.center, shape.half, desc.params[0]);
        });
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
        result =
            shadeField(frame, desc, transform, local, [&](Vector2 p) { return math::arc(p, arc); });
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
        result = shadeShadow(frame, desc, transform, local);
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
