#pragma once

#include "math/Color.h"
#include "math/WavetableWarp.h"
#include "render/FrameView.h"
#include "render/effects/FloatImage.h"

#include <array>
#include <cstdint>

namespace prism::render {
struct alignas(16) Wavetable3dUniforms {
    float rect[4];
    float camera[4];
    float projection[4];
    float shape[4];
    std::uint32_t slices;
    std::uint32_t phases;
    std::uint32_t voices;
    std::uint32_t flags;
    float warp[12];
};
static_assert(sizeof(Wavetable3dUniforms) == 128);
struct WavetableLine {
    std::array<math::Vector2, 256> points{};
    std::array<float, 256> depth{};
    std::uint32_t count = 0;
    std::uint32_t slice = 0;
    float radius = 0;
};
[[nodiscard]] bool wavetableActiveLine(const FrameView &frame, const PrimitiveDesc &desc,
                                       WavetableLine &line) noexcept;
[[nodiscard]] float wavetableSlice(const Wavetable3dUniforms &u, std::uint32_t index, bool &active,
                                   int &voice) noexcept;
void rasterWavetable3d(const FrameView &frame, const PrimitiveDesc &desc, FloatImage &image,
                       std::uint32_t width, std::uint32_t height);
[[nodiscard]] math::PremultipliedColor
shadeWavetable3d(const FrameView &frame, const PrimitiveDesc &desc, double x, double y) noexcept;
}
