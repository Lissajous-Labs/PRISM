#include "render/FormantCurve.h"

#include "math/Coverage.h"
#include "render/PrimitiveShading.h"
#include "render/ShadingSupport.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace prism::render {
namespace {
struct CurveSample {
    double height;
    double inverseSlopeNorm;
    math::Vector2 physicalNormal;
    double physicalNorm;
};
CurveSample curveSample(const FormantPayload &h, double dbRange, const TransformRecord &t,
                        double x) noexcept {
    const double height = formantHeight(h, dbRange, x);
    const double slope = formantHeight(h, dbRange, x + 0.5) - formantHeight(h, dbRange, x - 0.5);
    const double inverse = 1 / std::hypot(slope, 1.0);
    const double nx = -slope * inverse, ny = inverse;
    const math::Vector2 normal{t.inverse[0] * nx + t.inverse[1] * ny,
                               t.inverse[2] * nx + t.inverse[3] * ny};
    return {height, inverse, normal, std::hypot(normal.x, normal.y)};
}
math::PremultipliedColor curveColor(const FormantPayload &h, double dbRange,
                                    const TransformRecord &t, const CurveSample &s,
                                    math::PremultipliedColor color, math::Vector2 local) noexcept {
    if (local.x < h.rect[0] || local.x > h.rect[2] || local.y < h.rect[1] || local.y > h.rect[3]) {
        return {};
    }
    const double perpendicular = std::abs(local.y - s.height) * s.inverseSlopeNorm;
    const double core =
        math::boxCoverage((perpendicular - h.style[0] * 0.5) / s.physicalNorm, s.physicalNormal);
    const double under =
        local.y > s.height
            ? h.style[3] *
                  (h.fillDecay > 0
                       ? std::exp(-h.fillDecay * (local.y - s.height) / (h.rect[3] - h.rect[1]))
                       : 1.0)
            : 0;
    double refLine = 0;
    if (h.zeroLineAlpha > 0) {
        const double zeroNorm = std::clamp(-h.domain[3] / dbRange, 0.0, 1.0);
        const double zeroY = h.rect[1] + (h.rect[3] - h.rect[1]) * (1 - zeroNorm);
        const math::Vector2 lineNormal{t.inverse[1], t.inverse[3]};
        const double lineNorm = std::hypot(lineNormal.x, lineNormal.y);
        const double lineCov =
            lineNorm > 1e-12
                ? math::boxCoverage((std::abs(local.y - zeroY) - 0.5) / lineNorm, lineNormal)
                : 0;
        refLine = h.zeroLineAlpha * lineCov;
    }
    const double body = std::max(under, refLine);
    const double coverage = core + (1 - core) * body;
    const double halo =
        h.style[1] > 0
            ? h.style[2] * std::exp(-perpendicular * perpendicular / (2 * h.style[1] * h.style[1]))
            : 0;
    return {color.r * (coverage + halo), color.g * (coverage + halo), color.b * (coverage + halo),
            color.a * coverage};
}
}
double formantHeight(const FormantPayload &h, double dbRange, double x) noexcept {
    const double u = (x - h.rect[0]) / (h.rect[2] - h.rect[0]);
    const double omega = std::exp(h.domain[0] + u * h.domain[1]) * h.domain[2];
    const double sineHalf = std::sin(omega * 0.5), cosineHalf = std::cos(omega * 0.5);
    const double sine = 2 * sineHalf * cosineHalf, twiceSquare = 2 * sineHalf * sineHalf;
    double real = h.parallel ? 0 : 1, imaginary = 0;
    for (std::uint32_t i = 0; i < h.sections; ++i) {
        const auto &s = h.coefficients[i];
        const double nr = s[0] - s[1] * twiceSquare, ni = s[2] * sine;
        const double dr = s[3] - (2 - s[4]) * twiceSquare, di = s[4] * sine;
        const double denominator = std::max(dr * dr + di * di, 1e-30);
        const double r = (nr * dr + ni * di) / denominator, im = (ni * dr - nr * di) / denominator;
        if (h.parallel) {
            real += r;
            imaginary += im;
        } else {
            const double previous = real;
            real = previous * r - imaginary * im;
            imaginary = previous * im + imaginary * r;
        }
    }
    const double db = 10 * std::log10(std::max(real * real + imaginary * imaginary, 1e-24));
    const double normalized = std::clamp((db - h.domain[3]) / dbRange, 0.0, 1.0);
    return h.rect[1] + (h.rect[3] - h.rect[1]) * (1 - normalized);
}
math::PremultipliedColor shadeFormantCurve(const FrameView &frame, const PrimitiveDesc &desc,
                                           math::Vector2 local) noexcept {
    FormantPayload h;
    std::memcpy(&h, frame.payload.data() + desc.payloadOffset, sizeof(h));
    const auto &t = frame.transforms[desc.transformId];
    return curveColor(h, desc.params[0], t, curveSample(h, desc.params[0], t, local.x),
                      detail::decodeColor(desc.color), local);
}
void rasterFormantCurve(const FrameView &frame, const PrimitiveDesc &desc, FloatImage &image,
                        std::uint32_t width, std::uint32_t height) {
    image.resize(width, height);
    FormantPayload h;
    std::memcpy(&h, frame.payload.data() + desc.payloadOffset, sizeof(h));
    const auto &t = frame.transforms[desc.transformId];
    const auto color = detail::decodeColor(desc.color);
    for (std::uint32_t x = 0; x < width; ++x) {
        const double px = x + 0.5;
        const bool aligned = t.inverse[1] == 0 && t.inverse[2] == 0;
        const auto sample =
            aligned ? curveSample(h, desc.params[0], t, t.inverse[0] * px + t.inverse[4])
                    : CurveSample{};
        for (std::uint32_t y = 0; y < height; ++y) {
            const double py = y + 0.5;
            const auto local = detail::toLocal(t, px, py);
            const auto value =
                curveColor(h, desc.params[0], t,
                           aligned ? sample : curveSample(h, desc.params[0], t, local.x), color,
                           local);
            const double clip = clipCoverage(frame, desc.clipId, px, py);
            image.at(x,
                     y) = {static_cast<float>(value.r * clip), static_cast<float>(value.g * clip),
                           static_cast<float>(value.b * clip), static_cast<float>(value.a * clip)};
        }
    }
}
}
