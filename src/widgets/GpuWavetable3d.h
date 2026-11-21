#pragma once

#include "render/DrawList.h"
#include "render/Wavetable3d.h"

#include <vector>

namespace prism::widgets {
struct Wavetable3dSettings {
    math::WavetableCamera camera;
    math::WavetableWarp warp;
    float position = 0.5f;
    float morph3d = 1;
    float baseline = -1;
    float ambientFocus = 0.22f;
    float ribbonDepth = 0;
    float voiceSpread = 0.025f;
    float time = 0;
    float detune = 0;
    float phaseSpread = 0;
    std::uint32_t slices = 16;
    std::uint32_t phases = 256;
    std::uint32_t voices = 0;
    bool studioShading = false;
};
class GpuWavetable3d {
  public:
    GpuWavetable3d();
    [[nodiscard]] bool build(math::WavetableView table) noexcept;
    [[nodiscard]] render::TextureView field() const noexcept {
        return {256, 256, 1, field_, hal::PixelFormat::r16Float};
    }
    [[nodiscard]] render::Wavetable3dUniforms
    uniforms(render::RectF rect, const Wavetable3dSettings &settings) const noexcept;
    void draw(render::DrawList &list, render::RectF rect, std::uint32_t source,
              std::uint32_t palette, const Wavetable3dSettings &settings = {}) const;

  private:
    std::vector<float> field_;
};
}
