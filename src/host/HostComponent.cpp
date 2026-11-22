#include "host/HostComponent.h"

#include <algorithm>
#include <cmath>

namespace prism::host {
hal::Extent HostComponent::targetExtent(PeerBounds bounds) const noexcept {
    const hal::Extent drawable = surfaceHost_.drawableExtent();
    if (drawable.width > 0 && drawable.height > 0) {
        return drawable;
    }
    return {static_cast<std::uint32_t>(std::max(1.0, std::round(bounds.width))),
            static_cast<std::uint32_t>(std::max(1.0, std::round(bounds.height)))};
}

void HostComponent::drainSurface(void *self) {
    auto *component = static_cast<HostComponent *>(self);
    if (component->surface_) {
        component->surface_->waitIdle();
        ++component->drains_;
    }
}

void HostComponent::releaseSurface(void *self) {
    auto *component = static_cast<HostComponent *>(self);
    component->surface_.reset();
    component->surfaceHost_.detach();
}

bool HostComponent::attachToPeer(void *peerView, PeerBounds bounds) {
    lifecycle_.setDrain({&HostComponent::drainSurface, this});
    lifecycle_.setRelease({&HostComponent::releaseSurface, this});
    if (!lifecycle_.handle(SurfaceEvent::peerValid)) {
        return false;
    }
    if (PassiveSurfaceHost::supported() && !surfaceHost_.attach(peerView, bounds)) {
        lifecycle_.handle(SurfaceEvent::detach);
        return false;
    }
    surface_ = device_->createRenderSurface({targetExtent(bounds)});
    if (!surface_) {
        surfaceHost_.detach();
        lifecycle_.handle(SurfaceEvent::detach);
        return false;
    }
    fullRedraw_ = true;
    return lifecycle_.handle(SurfaceEvent::surfaceOk);
}

void HostComponent::setBounds(PeerBounds bounds) {
    surfaceHost_.setBounds(bounds);
    if (surface_) {
        const hal::Extent extent = targetExtent(bounds);
        if (extent.width != surface_->extent().width ||
            extent.height != surface_->extent().height) {
            surface_->resize(extent);
            fullRedraw_ = true;
        }
    }
}

void HostComponent::setOccluded(bool occluded) {
    if (lifecycle_.handle(occluded ? SurfaceEvent::occluded : SurfaceEvent::revealed) &&
        !occluded) {
        fullRedraw_ = true;
    }
}

bool HostComponent::renderFrame(const render::FrameView &view, const hal::DamageRegion *damage,
                                math::PremultipliedColor clear) {
    if (!lifecycle_.canRender() || !surface_) {
        return false;
    }
    if (surface_->lost()) {
        lifecycle_.handle(SurfaceEvent::gpuFault);
        if (!surface_->rebuild()) {
            return false;
        }
        lifecycle_.handle(SurfaceEvent::rebuilt);
        fullRedraw_ = true;
    }
    const hal::Extent extent = surface_->extent();
    const hal::DamageRegion full(
        {0, 0, static_cast<std::int32_t>(extent.width), static_cast<std::int32_t>(extent.height)});
    const hal::DamageRegion *region = fullRedraw_ || damage == nullptr ? &full : damage;
    if (region->empty()) {
        return true;
    }
    bool presented = false;
    const render::FrameRequest request{view, region, clear};
    const bool rendered = surfaceHost_.attached()
                              ? surfaceHost_.renderFrame(*surface_, request, presented)
                              : surface_->render(request);
    if (!rendered) {
        lifecycle_.handle(SurfaceEvent::gpuFault);
        return false;
    }
    fullRedraw_ = false;
    if (presented) {
        ++presented_;
    }
    return true;
}

void HostComponent::detach() { lifecycle_.handle(SurfaceEvent::detach); }
}
