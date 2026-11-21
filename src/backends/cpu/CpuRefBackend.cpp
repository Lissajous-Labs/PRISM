#include "backends/cpu/CpuRefBackend.h"

#include "render/Presentation.h"

namespace prism::backends {
CpuRefSurface::CpuRefSurface(hal::SurfaceDesc desc) : extent_(desc.extent) {
    raster_.resize(extent_.width, extent_.height);
}

void CpuRefSurface::resize(hal::Extent extent) {
    extent_ = extent;
    raster_.resize(extent.width, extent.height);
}

bool CpuRefSurface::rebuild() {
    raster_.resize(extent_.width, extent_.height);
    lost_ = false;
    return true;
}

bool CpuRefSurface::render(const render::FrameRequest &request) {
    if (lost_ || request.damage == nullptr) {
        return false;
    }
    raster_.render(request.view, *request.damage, request.clear);
    return true;
}

bool CpuRefSurface::readback(std::span<float> rgba) {
    const auto pixels = raster_.pixels();
    if (lost_ || rgba.size() != pixels.size() * 4) {
        return false;
    }
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        rgba[i * 4] = static_cast<float>(pixels[i].r);
        rgba[i * 4 + 1] = static_cast<float>(pixels[i].g);
        rgba[i * 4 + 2] = static_cast<float>(pixels[i].b);
        rgba[i * 4 + 3] = static_cast<float>(pixels[i].a);
    }
    return true;
}
}

namespace prism::backends {
bool CpuRefSurface::readbackPresented(std::span<std::uint8_t> rgba, std::uint32_t frame) {
    linear_.resize(rgba.size());
    if (!readback(linear_)) {
        return false;
    }
    render::presentDithered(linear_, extent_.width, extent_.height, frame, rgba);
    return true;
}
}
