#pragma once

#include "hal/DamageRegion.h"
#include "hal/DeviceRegistry.h"
#include "hal/SurfaceBackend.h"
#include "math/Color.h"
#include "render/FrameView.h"

#include <memory>
#include <span>

namespace prism::render {
struct FrameRequest {
    FrameView view;
    const hal::DamageRegion *damage;
    math::PremultipliedColor clear;
};

class RenderSurface : public hal::SurfaceBackend {
  public:
    [[nodiscard]] virtual bool render(const FrameRequest &request) = 0;
    [[nodiscard]] virtual bool readback(std::span<float> rgba) = 0;
};

class RenderDevice : public hal::BackendDevice {
  public:
    [[nodiscard]] virtual std::unique_ptr<RenderSurface>
    createRenderSurface(const hal::SurfaceDesc &desc) = 0;

    [[nodiscard]] std::unique_ptr<hal::SurfaceBackend>
    createSurface(const hal::SurfaceDesc &desc) final {
        return createRenderSurface(desc);
    }
};
}
