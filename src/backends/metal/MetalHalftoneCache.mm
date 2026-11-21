#include "backends/metal/MetalHalftoneCache.h"
#include "backends/metal/MetalUpload.h"

#include "render/HalftoneField.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace prism::backends {
namespace {
constexpr int cellMargin = 2;
}

void MetalHalftoneCache::prepare(MetalDeviceState &device, const render::FrameView &frame,
                                 id<MTLBuffer> primitives, id<MTLBuffer> payload) {
    jobs_.clear();
    auto *gpuPrimitives = static_cast<render::PrimitiveDesc *>(primitives.contents);
    auto *gpuPayload = static_cast<float *>(payload.contents);
    std::uint32_t total = 0;
    for (std::size_t i = 0; i < frame.primitives.size(); ++i) {
        const auto &d = frame.primitives[i];
        if (d.kind() != render::PrimitiveKind::halftoneField) {
            continue;
        }
        render::HalftonePayload h;
        std::memcpy(&h, frame.payload.data() + d.payloadOffset, sizeof(h));
        const double det = static_cast<double>(h.basis[0]) * h.basis[3] -
                           static_cast<double>(h.basis[1]) * h.basis[2];
        if (det == 0) {
            continue;
        }
        double lowX = std::numeric_limits<double>::infinity(), lowY = lowX;
        double highX = -lowX, highY = -lowX;
        for (const auto x : {h.rect[0], h.rect[2]}) {
            for (const auto y : {h.rect[1], h.rect[3]}) {
                const double rx = x - h.origin[0], ry = y - h.origin[1];
                const double u = (h.basis[3] * rx - h.basis[2] * ry) / det;
                const double v = (h.basis[0] * ry - h.basis[1] * rx) / det;
                lowX = std::min(lowX, u);
                highX = std::max(highX, u);
                lowY = std::min(lowY, v);
                highY = std::max(highY, v);
            }
        }
        const int x0 = static_cast<int>(std::floor(lowX)) - cellMargin;
        const int y0 = static_cast<int>(std::floor(lowY)) - cellMargin;
        const auto width =
            static_cast<std::uint32_t>(static_cast<int>(std::floor(highX)) + cellMargin + 1 - x0);
        const auto height =
            static_cast<std::uint32_t>(static_cast<int>(std::floor(highY)) + cellMargin + 1 - y0);
        gpuPrimitives[i].params[1] = static_cast<float>(x0);
        gpuPrimitives[i].params[2] = static_cast<float>(y0);
        gpuPrimitives[i].params[3] = static_cast<float>(width);
        std::memcpy(gpuPayload + d.payloadOffset + 31, &total, sizeof(total));
        jobs_.push_back(
            {static_cast<std::uint32_t>(i), width, height, total, d.auxHandle, h.blueNoiseSlot});
        total += width * height;
    }
    upload(device.device, cells_, nullptr, static_cast<std::size_t>(total) * 16);
}

void MetalHalftoneCache::encode(MetalDeviceState &device, id<MTLCommandBuffer> commands,
                                id<MTLBuffer> primitives, id<MTLBuffer> payload,
                                id<MTLTexture> gradients,
                                const std::array<id<MTLTexture>, textureSlots> &textures) {
    if (jobs_.empty()) {
        return;
    }
    const auto slot = [&](std::uint32_t index) {
        return index < textures.size() && textures[index] != nil ? textures[index]
                                                                 : device.emptyTexture;
    };
    id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
    [encoder setComputePipelineState:device.halftoneCells];
    [encoder setBuffer:primitives offset:0 atIndex:0];
    [encoder setBuffer:payload offset:0 atIndex:1];
    [encoder setBuffer:cells_ offset:0 atIndex:2];
    [encoder setTexture:(gradients ? gradients : device.emptyTexture) atIndex:0];
    for (const auto &job : jobs_) {
        [encoder setBytes:&job length:4 * sizeof(std::uint32_t) atIndex:3];
        [encoder setTexture:slot(job.field) atIndex:1];
        [encoder setTexture:slot(job.noise) atIndex:2];
        [encoder dispatchThreads:MTLSizeMake(job.width, job.height, 1)
            threadsPerThreadgroup:MTLSizeMake(16, 8, 1)];
    }
    [encoder endEncoding];
}
}
