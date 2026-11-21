#pragma once

#include "hal/HalTypes.h"

namespace prism::hal {
struct SurfaceDesc {
    Extent extent;
    PixelFormat format = PixelFormat::rgba16Float;
};

class SurfaceBackend {
  public:
    virtual ~SurfaceBackend() = default;
    [[nodiscard]] virtual Extent extent() const noexcept = 0;
    virtual void resize(Extent extent) = 0;
    virtual void loseDevice() noexcept = 0;
    [[nodiscard]] virtual bool rebuild() = 0;
    [[nodiscard]] virtual bool lost() const noexcept = 0;
};
}
