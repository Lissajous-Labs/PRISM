#pragma once

#include "hal/DamageRegion.h"
#include "render/RenderSurface.h"

#include <cstdint>
#include <memory>

namespace prism::host {
struct PeerBounds {
    double x;
    double y;
    double width;
    double height;
};

struct FrameTick {
    void (*function)(void *, double) = nullptr;
    void *context = nullptr;
};

class PassiveSurfaceHost {
  public:
    PassiveSurfaceHost();
    ~PassiveSurfaceHost();
    PassiveSurfaceHost(const PassiveSurfaceHost &) = delete;
    PassiveSurfaceHost &operator=(const PassiveSurfaceHost &) = delete;

    [[nodiscard]] static bool supported() noexcept;

    [[nodiscard]] bool attach(void *peerView, PeerBounds bounds);
    void setBounds(PeerBounds bounds);
    void setLiveResize(bool active);
    [[nodiscard]] bool startFrameClock(FrameTick tick, bool preferLegacy = false);
    void stopFrameClock();
    void detach();

    [[nodiscard]] bool attached() const noexcept;
    [[nodiscard]] double contentsScale() const noexcept;
    [[nodiscard]] hal::Extent drawableExtent() const noexcept;
    [[nodiscard]] void *nativeView() const noexcept;
    [[nodiscard]] bool renderFrame(render::RenderSurface &surface,
                                   const render::FrameRequest &request, bool &presented);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
