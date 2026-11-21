#pragma once

#include "core/FunctionRef.h"
#include "host/ProcessorState.h"
#include "math/Vector2.h"
#include "render/DrawList.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string_view>

namespace prism::widgets {
enum class PointerKind : std::uint8_t { down, move, up, wheel, doubleClick };

struct PointerEvent {
    PointerKind kind;
    math::Vector2 position;
    float wheel = 0;
    bool fine = false;
};

using TextPainter =
    FunctionRef<void(render::DrawList &, std::string_view, math::Vector2, std::uint32_t)>;

struct WidgetColors {
    std::uint32_t track = render::packColor(40, 40, 44, 255);
    std::uint32_t body = render::packColor(128, 128, 128, 255);
    std::uint32_t ring = render::packColor(6, 6, 6, 255);
    std::uint32_t accent = render::packColor(160, 226, 100, 255);
    std::uint32_t pointer = render::packColor(250, 250, 250, 255);
    std::uint32_t surface = render::packColor(176, 176, 176, 255);
    std::uint32_t text = render::packColor(28, 28, 28, 255);
};

class BoundParameter {
  public:
    BoundParameter(host::ParameterMirror &mirror, host::HostParameterSink &sink,
                   std::uint32_t index) noexcept
        : mirror_(&mirror), sink_(&sink), index_(index) {}

    [[nodiscard]] float value() const noexcept { return mirror_->normalized(index_); }
    [[nodiscard]] float defaultValue() const noexcept {
        return mirror_->toNormalized(index_, mirror_->spec(index_).defaultValue);
    }
    void begin() const { mirror_->beginEdit(index_, *sink_); }
    void set(float normalized) const {
        mirror_->edit(index_, std::clamp(normalized, 0.0f, 1.0f), *sink_);
    }
    void end() const { mirror_->endEdit(index_, *sink_); }
    [[nodiscard]] std::size_t steps() const noexcept {
        const host::ParameterSpec &spec = mirror_->spec(index_);
        return spec.step > 0 ? static_cast<std::size_t>(
                                   std::lround((spec.maximum - spec.minimum) / spec.step))
                             : 0;
    }

  private:
    host::ParameterMirror *mirror_;
    host::HostParameterSink *sink_;
    std::uint32_t index_;
};

[[nodiscard]] inline bool inside(render::RectF r, math::Vector2 p) noexcept {
    return p.x >= r.x0 && p.x < r.x1 && p.y >= r.y0 && p.y < r.y1;
}

[[nodiscard]] inline float screenAngle(float clockwiseDegrees) noexcept {
    return (clockwiseDegrees - 90.0f) * std::numbers::pi_v<float> / 180.0f;
}
}
