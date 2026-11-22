#include "backends/metal/MetalShadowCache.h"
#include "backends/metal/MetalUpload.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace prism::backends {
namespace {
constexpr std::size_t maxEntries = 256;
constexpr std::uint32_t maxTexels = 1u << 22;

float largestScale(const render::TransformRecord &t) noexcept {
    const double a = t.forward[0], b = t.forward[1], c = t.forward[2], d = t.forward[3];
    const double sum = a * a + b * b + c * c + d * d;
    const double determinant = a * d - b * c;
    const double root = std::sqrt(std::max(0.0, sum * sum / 4 - determinant * determinant));
    return static_cast<float>(std::sqrt(sum / 2 + root));
}
}

void MetalShadowCache::prepare(MetalDeviceState &device, const render::FrameView &frame,
                               id<MTLBuffer> primitives, bool present) {
    pending_.clear();
    bool stepsReady = false;
    std::size_t recent = 0;
    auto *gpuPrimitives = static_cast<render::PrimitiveDesc *>(primitives.contents);
    const std::size_t scanned = present ? frame.primitives.size() : 0;
    for (std::size_t i = 0; i < scanned; ++i) {
        const auto &d = frame.primitives[i];
        if (d.kind() != render::PrimitiveKind::boxShadow || d.has(render::flags::inset)) {
            continue;
        }
        const auto box = d.shapeRect();
        const float half[2] = {(box[2] - box[0]) / 2, (box[3] - box[1]) / 2};
        const float radius = std::clamp(d.params[1], 0.0f, std::min(half[0], half[1]));
        const float sigma = d.params[0];
        if (!(sigma > 0)) {
            continue;
        }
        if (!stepsReady) {
            sampleSteps_.assign(frame.transforms.size(), -1);
            stepsReady = true;
        }
        float &step = sampleSteps_[d.transformId];
        if (step < 0) {
            const float scale = largestScale(frame.transforms[d.transformId]);
            step = 0;
            if (std::isfinite(scale) && scale > 0) {
                int exponent;
                static_cast<void>(std::frexp(scale, &exponent));
                step = std::ldexp(1.0f, -exponent);
            }
        }
        if (!(step > 0)) {
            continue;
        }
        const auto matches = [&](const ShadowSprite &e) {
            return e.step == step && e.half[0] == half[0] && e.half[1] == half[1] &&
                   e.radius == radius && e.sigma == sigma;
        };
        const auto found = recent < entries_.size() && matches(entries_[recent])
                               ? entries_.begin() + static_cast<std::ptrdiff_t>(recent)
                               : std::find_if(entries_.begin(), entries_.end(), matches);
        std::uint32_t index;
        if (found != entries_.end()) {
            index = static_cast<std::uint32_t>(found - entries_.begin());
        } else {
            const float reach = 3 * sigma + 2;
            const auto width = static_cast<std::uint32_t>(std::ceil((half[0] + reach) / step)) + 2;
            const auto height = static_cast<std::uint32_t>(std::ceil((half[1] + reach) / step)) + 2;
            if (entries_.size() >= maxEntries || texels_ + width * height > maxTexels) {
                entries_.clear();
                texels_ = 0;
                pending_.clear();
                for (std::size_t j = 0; j < i; ++j) {
                    gpuPrimitives[j].auxHandle =
                        frame.primitives[j].kind() == render::PrimitiveKind::boxShadow
                            ? render::noTexture
                            : gpuPrimitives[j].auxHandle;
                }
            }
            if (width * height > maxTexels) {
                continue;
            }
            index = static_cast<std::uint32_t>(entries_.size());
            entries_.push_back({texels_, width, height, step, {half[0], half[1]}, radius, sigma});
            texels_ += width * height;
            pending_.push_back(index);
        }
        gpuPrimitives[i].auxHandle = index;
        recent = index;
    }
    if (!pending_.empty() || table_ == nil) {
        upload(device.device, table_, entries_.data(), entries_.size() * sizeof(ShadowSprite));
        if (sprites_ == nil || sprites_.length < static_cast<std::size_t>(maxTexels) * 4) {
            sprites_ = [device.device newBufferWithLength:static_cast<std::size_t>(maxTexels) * 4
                                                  options:MTLResourceStorageModePrivate];
        }
    }
}

void MetalShadowCache::encode(MetalDeviceState &device, id<MTLCommandBuffer> commands) {
    if (pending_.empty()) {
        return;
    }
    id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
    [encoder setComputePipelineState:device.shadowSprites];
    [encoder setBuffer:table_ offset:0 atIndex:0];
    [encoder setBuffer:sprites_ offset:0 atIndex:1];
    for (const std::uint32_t index : pending_) {
        const ShadowSprite &entry = entries_[index];
        [encoder setBytes:&index length:sizeof(index) atIndex:2];
        [encoder dispatchThreads:MTLSizeMake(entry.width, entry.height, 1)
            threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
    }
    [encoder endEncoding];
    pending_.clear();
}
}
