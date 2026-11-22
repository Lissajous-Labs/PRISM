#pragma once

#include "host/PassiveSurfaceHost.h"
#include "host/SurfaceLifecycle.h"
#include "render/RenderSurface.h"

#include <cstdint>
#include <memory>

namespace prism::host {
class HostComponent {
  public:
    explicit HostComponent(render::RenderDevice &device) : device_(&device) {}
    ~HostComponent() { detach(); }
    HostComponent(const HostComponent &) = delete;
    HostComponent &operator=(const HostComponent &) = delete;

    [[nodiscard]] bool attachToPeer(void *peerView, PeerBounds bounds);
    void setBounds(PeerBounds bounds);
    void setLiveResize(bool active) { surfaceHost_.setLiveResize(active); }
    void setOccluded(bool occluded);
    [[nodiscard]] bool renderFrame(const render::FrameView &view, const hal::DamageRegion *damage,
                                   math::PremultipliedColor clear);
    void detach();

    [[nodiscard]] SurfaceState state() const noexcept { return lifecycle_.state(); }
    [[nodiscard]] const SurfaceLifecycle &lifecycle() const noexcept { return lifecycle_; }
    [[nodiscard]] render::RenderSurface *surface() noexcept { return surface_.get(); }
    [[nodiscard]] PassiveSurfaceHost &surfaceHost() noexcept { return surfaceHost_; }
    [[nodiscard]] std::uint32_t presentedFrames() const noexcept { return presented_; }
    [[nodiscard]] std::uint32_t drains() const noexcept { return drains_; }

  private:
    render::RenderDevice *device_;
    SurfaceLifecycle lifecycle_;
    PassiveSurfaceHost surfaceHost_;
    std::unique_ptr<render::RenderSurface> surface_;
    std::uint32_t presented_ = 0;
    std::uint32_t drains_ = 0;
    bool fullRedraw_ = true;

    [[nodiscard]] hal::Extent targetExtent(PeerBounds bounds) const noexcept;
    static void drainSurface(void *self);
    static void releaseSurface(void *self);
};
}
