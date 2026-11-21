#include "render/DrawList.h"

#include <algorithm>
#include <cmath>

namespace prism::render {
namespace {
RectF pointsBounds(std::initializer_list<math::Vector2> points, float margin) {
    RectF bounds{INFINITY, INFINITY, -INFINITY, -INFINITY};
    for (const auto &point : points) {
        bounds.x0 = std::min(bounds.x0, static_cast<float>(point.x));
        bounds.y0 = std::min(bounds.y0, static_cast<float>(point.y));
        bounds.x1 = std::max(bounds.x1, static_cast<float>(point.x));
        bounds.y1 = std::max(bounds.y1, static_cast<float>(point.y));
    }
    return {bounds.x0 - margin, bounds.y0 - margin, bounds.x1 + margin, bounds.y1 + margin};
}

constexpr float aaMargin = 1.5f;
}

DrawList::DrawList() { clear(); }

void DrawList::clear() {
    primitives_.clear();
    payload_.clear();
    transforms_.clear();
    clips_.clear();
    transforms_.push_back(*TransformRecord::from(math::Affine2D{}));
    clips_.push_back({noClip, identityTransform, ClipKind::none, {}, {}});
    transform_ = identityTransform;
    clip_ = noClip;
    blend_ = hal::BlendMode::sourceOver;
    gradientRow_ = noTexture;
}

std::optional<std::uint16_t> DrawList::addTransform(const math::Affine2D &localToPhysical) {
    const auto record = TransformRecord::from(localToPhysical);
    if (!record || transforms_.size() > 0xFFFF) {
        return std::nullopt;
    }
    transforms_.push_back(*record);
    return static_cast<std::uint16_t>(transforms_.size() - 1);
}

std::uint16_t DrawList::addClip(ClipKind kind, RectF bounds, math::CornerRadii radii) {
    clips_.push_back({clip_,
                      transform_,
                      kind,
                      {bounds.x0, bounds.y0, bounds.x1, bounds.y1},
                      {static_cast<float>(radii.topRight), static_cast<float>(radii.bottomRight),
                       static_cast<float>(radii.topLeft), static_cast<float>(radii.bottomLeft)}});
    return static_cast<std::uint16_t>(clips_.size() - 1);
}

void DrawList::setGradient(std::uint32_t row, math::Vector2 start, math::Vector2 end) noexcept {
    gradientRow_ = row;
    gradientGeometry_[0] = static_cast<float>(start.x);
    gradientGeometry_[1] = static_cast<float>(start.y);
    gradientGeometry_[2] = static_cast<float>(end.x);
    gradientGeometry_[3] = static_cast<float>(end.y);
}

PrimitiveDesc &DrawList::emit(PrimitiveKind kind, RectF bounds, std::uint32_t color) {
    PrimitiveDesc &desc = primitives_.emplace_back();
    desc = {};
    desc.bounds[0] = bounds.x0;
    desc.bounds[1] = bounds.y0;
    desc.bounds[2] = bounds.x1;
    desc.bounds[3] = bounds.y1;
    desc.kindFlags = static_cast<std::uint32_t>(kind) |
                     (static_cast<std::uint32_t>(blend_) << flags::blendShift);
    desc.color = color;
    desc.depth = static_cast<std::uint32_t>(primitives_.size());
    desc.transformId = transform_;
    desc.clipId = clip_;
    desc.auxHandle = noTexture;
    if (gradientRow_ != noTexture && kind <= PrimitiveKind::triangle) {
        desc.kindFlags |= flags::gradient;
        desc.auxHandle = gradientRow_;
        desc.gradientOffset = appendPayload({gradientGeometry_[0], gradientGeometry_[1],
                                             gradientGeometry_[2], gradientGeometry_[3]});
    }
    return desc;
}

std::uint32_t DrawList::appendPayload(std::initializer_list<float> values) {
    const auto offset = static_cast<std::uint32_t>(payload_.size());
    payload_.insert(payload_.end(), values);
    return offset;
}

void DrawList::roundedRect(RectF rect, math::CornerRadii radii, std::uint32_t color,
                           std::optional<BorderStyle> border) {
    const math::Vector2 half{(rect.x1 - rect.x0) / 2.0, (rect.y1 - rect.y0) / 2.0};
    const auto clamped = math::clampRadii(radii, half);
    PrimitiveDesc &desc = emit(
        PrimitiveKind::roundedRect,
        {rect.x0 - aaMargin, rect.y0 - aaMargin, rect.x1 + aaMargin, rect.y1 + aaMargin}, color);
    desc.params[0] = static_cast<float>(clamped.topRight);
    desc.params[1] = static_cast<float>(clamped.bottomRight);
    desc.params[2] = static_cast<float>(clamped.topLeft);
    desc.params[3] = static_cast<float>(clamped.bottomLeft);
    desc.payloadOffset = appendPayload({rect.x0, rect.y0, rect.x1, rect.y1});
    if (border) {
        desc.kindFlags |= flags::border;
        desc.borderColor = border->color;
        static_cast<void>(appendPayload(
            {static_cast<float>(border->widths.left), static_cast<float>(border->widths.top),
             static_cast<float>(border->widths.right), static_cast<float>(border->widths.bottom)}));
    }
    const bool solid = (color >> 24) == 255 && (!border || (border->color >> 24) == 255);
    if (solid && blend_ == hal::BlendMode::sourceOver && gradientRow_ == noTexture) {
        desc.kindFlags |= flags::opaque;
    }
}

void DrawList::squircle(RectF rect, float power, std::uint32_t color) {
    PrimitiveDesc &desc = emit(
        PrimitiveKind::squircle,
        {rect.x0 - aaMargin, rect.y0 - aaMargin, rect.x1 + aaMargin, rect.y1 + aaMargin}, color);
    desc.params[0] = power;
    desc.payloadOffset = appendPayload({rect.x0, rect.y0, rect.x1, rect.y1});
}

void DrawList::arc(math::Vector2 center, float radius, float thickness, float midAngle,
                   float halfAperture, math::ArcCap cap, std::uint32_t color) {
    const float reach = radius + thickness / 2 + aaMargin;
    PrimitiveDesc &desc =
        emit(PrimitiveKind::circleArc,
             {static_cast<float>(center.x) - reach, static_cast<float>(center.y) - reach,
              static_cast<float>(center.x) + reach, static_cast<float>(center.y) + reach},
             color);
    desc.kindFlags |= static_cast<std::uint32_t>(cap) << flags::capShift;
    desc.params[0] = radius;
    desc.params[1] = thickness;
    desc.params[2] = midAngle;
    desc.params[3] = halfAperture;
    desc.payloadOffset =
        appendPayload({static_cast<float>(center.x), static_cast<float>(center.y)});
}

void DrawList::capsule(math::Vector2 a, math::Vector2 b, float radius, std::uint32_t color) {
    PrimitiveDesc &desc =
        emit(PrimitiveKind::lineCapsule, pointsBounds({a, b}, radius + aaMargin), color);
    desc.params[0] = radius;
    desc.payloadOffset = appendPayload({static_cast<float>(a.x), static_cast<float>(a.y),
                                        static_cast<float>(b.x), static_cast<float>(b.y)});
}

void DrawList::quadBezier(math::Vector2 p0, math::Vector2 p1, math::Vector2 p2, float halfWidth,
                          std::uint32_t color) {
    PrimitiveDesc &desc =
        emit(PrimitiveKind::quadBezier, pointsBounds({p0, p1, p2}, halfWidth + aaMargin), color);
    desc.params[0] = halfWidth;
    desc.payloadOffset = appendPayload({static_cast<float>(p0.x), static_cast<float>(p0.y),
                                        static_cast<float>(p1.x), static_cast<float>(p1.y),
                                        static_cast<float>(p2.x), static_cast<float>(p2.y)});
}

void DrawList::triangle(math::Vector2 a, math::Vector2 b, math::Vector2 c, float radius,
                        std::uint32_t color) {
    PrimitiveDesc &desc = emit(PrimitiveKind::triangle, pointsBounds({a, b, c}, aaMargin), color);
    desc.params[0] = radius;
    desc.payloadOffset =
        appendPayload({static_cast<float>(a.x), static_cast<float>(a.y), static_cast<float>(b.x),
                       static_cast<float>(b.y), static_cast<float>(c.x), static_cast<float>(c.y)});
}

void DrawList::boxShadow(RectF box, float radius, float sigma, std::uint32_t color, bool inset) {
    const float reach = inset ? aaMargin : 3 * sigma + aaMargin;
    PrimitiveDesc &desc =
        emit(PrimitiveKind::boxShadow,
             {box.x0 - reach, box.y0 - reach, box.x1 + reach, box.y1 + reach}, color);
    if (inset) {
        desc.kindFlags |= flags::inset;
    }
    desc.params[0] = sigma;
    desc.params[1] = radius;
    desc.payloadOffset = appendPayload({box.x0, box.y0, box.x1, box.y1});
}

void DrawList::texturedQuad(PrimitiveKind kind, RectF rect, RectF uv, std::uint32_t texture,
                            std::uint32_t color, float pixelRange) {
    PrimitiveDesc &desc = emit(kind, rect, color);
    desc.auxHandle = texture;
    desc.params[0] = pixelRange;
    desc.payloadOffset = appendPayload({uv.x0, uv.y0, uv.x1, uv.y1});
}

void DrawList::plotStrip(RectF rect, std::uint32_t strip, std::uint32_t columns, float thickness,
                         std::uint32_t color) {
    PrimitiveDesc &desc = emit(PrimitiveKind::plotStrip, rect, color);
    desc.auxHandle = strip;
    desc.params[0] = thickness;
    desc.params[1] = static_cast<float>(columns);
    desc.payloadOffset = appendPayload({rect.x0, rect.y0, rect.x1, rect.y1});
}
}
