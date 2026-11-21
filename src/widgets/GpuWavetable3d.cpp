#include "widgets/GpuWavetable3d.h"

#include "math/HalfFloat.h"

#include <algorithm>
#include <cmath>

namespace prism::widgets {
GpuWavetable3d::GpuWavetable3d() : field_(256 * 256) {}
bool GpuWavetable3d::build(math::WavetableView table) noexcept {
    if (table.frames == 0 || table.period == 0 ||
        table.samples.size() / table.period < table.frames) {
        return false;
    }
    for (std::size_t m = 0; m < 256; ++m) {
        for (std::size_t n = 0; n < 256; ++n) {
            const double value = table.sample(math::wavetableTurn * static_cast<double>(n) / 256,
                                              static_cast<double>(m) / 255);
            field_[m * 256 + n] = math::halfRound(static_cast<float>(value));
        }
    }
    return true;
}
render::Wavetable3dUniforms
GpuWavetable3d::uniforms(render::RectF rect, const Wavetable3dSettings &settings) const noexcept {
    auto camera = settings.camera;
    const double morph = std::clamp(static_cast<double>(settings.morph3d), 0.0, 1.0);
    camera.yaw *= morph;
    camera.pitch *= morph;
    camera.inverseDistance *= morph;
    render::Wavetable3dUniforms u{};
    u.rect[0] = rect.x0;
    u.rect[1] = rect.y0;
    u.rect[2] = rect.x1;
    u.rect[3] = rect.y1;
    u.camera[0] = static_cast<float>(std::cos(camera.yaw));
    u.camera[1] = static_cast<float>(std::sin(camera.yaw));
    u.camera[2] = static_cast<float>(std::cos(camera.pitch));
    u.camera[3] = static_cast<float>(std::sin(camera.pitch));
    u.projection[0] = static_cast<float>(camera.inverseDistance);
    u.projection[1] = static_cast<float>(camera.amplitudeScale);
    u.projection[2] = static_cast<float>(camera.fit(rect.x1 - rect.x0, rect.y1 - rect.y0));
    u.projection[3] = static_cast<float>(std::abs(std::cos(camera.pitch) * std::sin(camera.yaw)) +
                                         std::abs(std::sin(camera.pitch) * camera.amplitudeScale) +
                                         std::abs(std::cos(camera.pitch) * std::cos(camera.yaw)));
    u.shape[0] = std::clamp(settings.position, 0.0f, 1.0f);
    u.shape[1] = settings.baseline;
    u.shape[2] = settings.voiceSpread;
    u.shape[3] = 0.22f;
    u.slices = std::clamp(settings.slices, 2u, 20u);
    u.phases = settings.phases == 128 ? 128 : 256;
    u.voices = std::min(settings.voices, 4u);
    u.flags = camera.descendingSlices() ? 1 : 0;
    const auto &w = settings.warp;
    const double values[]{w.mirror,
                          w.bend,
                          w.sync,
                          w.phaseOffset,
                          w.quantization,
                          w.sharpness,
                          w.amplitudeModulation,
                          w.modulationHarmonic,
                          w.modulationPhase,
                          settings.time,
                          settings.detune,
                          settings.phaseSpread};
    for (std::size_t i = 0; i < 12; ++i) {
        u.warp[i] = static_cast<float>(values[i]);
    }
    return u;
}
void GpuWavetable3d::draw(render::DrawList &list, render::RectF rect, std::uint32_t source,
                          std::uint32_t palette, const Wavetable3dSettings &settings) const {
    list.wavetable3d(uniforms(rect, settings), source, palette);
}
}
