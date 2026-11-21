#include "host/PassiveSurfaceHost.h"

namespace prism::host {
struct PassiveSurfaceHost::Impl {};

PassiveSurfaceHost::PassiveSurfaceHost() : impl_(std::make_unique<Impl>()) {}

PassiveSurfaceHost::~PassiveSurfaceHost() = default;

bool PassiveSurfaceHost::supported() noexcept { return false; }

bool PassiveSurfaceHost::attach(void *, PeerBounds) { return false; }

void PassiveSurfaceHost::setBounds(PeerBounds) {}

void PassiveSurfaceHost::setLiveResize(bool) {}

void PassiveSurfaceHost::detach() {}

bool PassiveSurfaceHost::attached() const noexcept { return false; }

double PassiveSurfaceHost::contentsScale() const noexcept { return 0; }

hal::Extent PassiveSurfaceHost::drawableExtent() const noexcept { return {0, 0}; }

void *PassiveSurfaceHost::nativeView() const noexcept { return nullptr; }

bool PassiveSurfaceHost::present(render::RenderSurface &, std::uint32_t) { return false; }
}
