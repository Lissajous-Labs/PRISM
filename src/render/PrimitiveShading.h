#pragma once

#include "render/Blend.h"
#include "render/FrameView.h"

namespace prism::render {
[[nodiscard]] ShadeResult shadePrimitive(const FrameView &frame, const PrimitiveDesc &desc,
                                         double x, double y) noexcept;
[[nodiscard]] double clipCoverage(const FrameView &frame, std::uint16_t clipId, double x,
                                  double y) noexcept;
}
