#pragma once

#include "render/FrameView.h"

#include <array>

namespace prism::render {
class BlueNoise {
  public:
    BlueNoise();
    [[nodiscard]] TextureView texture() const noexcept {
        return {64, 64, 1, values_, hal::PixelFormat::r8Unorm, 1};
    }

  private:
    std::array<float, 4096> values_{};
};
}
