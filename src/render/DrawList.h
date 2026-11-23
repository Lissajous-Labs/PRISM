#pragma once

#include "hal/HalTypes.h"
#include "math/Affine2D.h"
#include "math/SdfCurves.h"
#include "math/SdfShapes.h"
#include "render/FormantCurve.h"
#include "render/FrameRecords.h"
#include "render/HalftoneField.h"
#include "render/PrimitiveDesc.h"
#include "render/Wavetable3d.h"
#include "render/effects/EffectDraw.h"
#include "render/vector/PathDraw.h"
#include "render/vector/StrokeExpander.h"
#include "render/vector/VectorPath.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace prism::render {
struct RectF {
    float x0;
    float y0;
    float x1;
    float y1;
};

struct BorderStyle {
    math::BorderWidths widths;
    std::uint32_t color;
};

class DrawList {
  public:
    DrawList();

    void clear();
    [[nodiscard]] std::optional<std::uint16_t> addTransform(const math::Affine2D &localToPhysical);
    [[nodiscard]] std::uint16_t addClip(ClipKind kind, RectF bounds, math::CornerRadii radii = {});
    void setTransform(std::uint16_t id) noexcept { transform_ = id; }
    void setClip(std::uint16_t id) noexcept { clip_ = id; }
    void setBlend(hal::BlendMode mode) noexcept { blend_ = mode; }
    void setLinearGradient(std::uint32_t row, math::Vector2 start, math::Vector2 end) noexcept;
    void setRadialGradient(std::uint32_t row, math::Vector2 center, math::Vector2 focus,
                           float radius) noexcept;
    void setConicGradient(std::uint32_t row, math::Vector2 center, float startAngle) noexcept;
    void clearGradient() noexcept { gradientRow_ = noTexture; }

    void roundedRect(RectF rect, math::CornerRadii radii, std::uint32_t color,
                     std::optional<BorderStyle> border = std::nullopt);
    void squircle(RectF rect, float power, std::uint32_t color, float cornerRadius = 0);
    void arc(math::Vector2 center, float radius, float thickness, float midAngle,
             float halfAperture, math::ArcCap cap, std::uint32_t color);
    void capsule(math::Vector2 a, math::Vector2 b, float radius, std::uint32_t color);
    void quadBezier(math::Vector2 p0, math::Vector2 p1, math::Vector2 p2, float halfWidth,
                    std::uint32_t color);
    void triangle(math::Vector2 a, math::Vector2 b, math::Vector2 c, float radius,
                  std::uint32_t color);
    void boxShadow(RectF box, float radius, float sigma, std::uint32_t color, bool inset = false);
    void texturedQuad(PrimitiveKind kind, RectF rect, RectF uv, std::uint32_t texture,
                      std::uint32_t color, float pixelRange = 0);
    void glyphQuad(PrimitiveKind kind, RectF rect, RectF uv, std::uint32_t texture,
                   std::uint32_t color, float pixelRange, float ppem, float background);
    void slugGlyph(RectF rect, RectF em, std::span<const float> blob, std::uint32_t color);
    void spectrumCurve(RectF rect, std::span<const float> heights, std::span<const float> slopes,
                       float strokeWidth, std::uint32_t color);
    void fillPath(const VectorPath &path, std::uint32_t color, FillRule rule = FillRule::nonZero);
    void strokePath(const VectorPath &path, const StrokeStyle &style, std::uint32_t color);
    void backdropBlur(RectF rect, math::CornerRadii radii, float sigma);
    void plotStrip(RectF rect, std::uint32_t strip, std::uint32_t columns, float thickness,
                   std::uint32_t color, float scrollColumns = 0, float edgeFade = 0,
                   bool fillBelow = false);
    void halftoneField(const HalftonePayload &field, std::uint32_t texture);
    void wavetable3d(const Wavetable3dUniforms &uniforms, std::uint32_t texture,
                     std::uint32_t palette);
    void formantCurve(const FormantPayload &curve, float dbRange, std::uint32_t color);

    [[nodiscard]] std::span<const PrimitiveDesc> primitives() const noexcept { return primitives_; }
    [[nodiscard]] std::span<const float> payload() const noexcept { return payload_; }
    [[nodiscard]] std::span<const TransformRecord> transforms() const noexcept {
        return transforms_;
    }
    [[nodiscard]] std::span<const ClipRecord> clips() const noexcept { return clips_; }
    [[nodiscard]] std::span<const PathDraw> paths() const noexcept { return paths_; }
    [[nodiscard]] std::span<const EffectDraw> effects() const noexcept { return effects_; }
    [[nodiscard]] std::span<const LineSegment> pathSegments() const noexcept {
        return pathSegments_;
    }

  private:
    std::vector<PrimitiveDesc> primitives_;
    std::vector<float> payload_;
    std::vector<TransformRecord> transforms_;
    std::vector<ClipRecord> clips_;
    std::vector<PathDraw> paths_;
    std::vector<EffectDraw> effects_;
    std::vector<LineSegment> pathSegments_;
    PolylineBuffer polylineScratch_;
    PolylineBuffer outlineScratch_;
    StrokeScratch strokeScratch_;
    std::uint16_t transform_ = identityTransform;
    std::uint16_t clip_ = noClip;
    hal::BlendMode blend_ = hal::BlendMode::sourceOver;
    std::uint32_t gradientRow_ = noTexture;
    GradientShape gradientShape_ = GradientShape::linear;
    float gradientGeometry_[6]{};

    PrimitiveDesc &emit(PrimitiveKind kind, RectF bounds, std::uint32_t color);
    std::uint32_t appendPayload(std::initializer_list<float> values);
    void emitPath(std::uint32_t firstSegment, std::uint32_t color, FillRule rule);
};
}
