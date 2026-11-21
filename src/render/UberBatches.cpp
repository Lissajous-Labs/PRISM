#include "render/UberBatches.h"

namespace prism::render {
UberPipeline pipelineFor(const PrimitiveDesc &desc) noexcept {
    if (desc.kind() == PrimitiveKind::glyphLcd) {
        return UberPipeline::subpixel;
    }
    switch (desc.blend()) {
    case hal::BlendMode::sourceOver:
        if (desc.kind() <= PrimitiveKind::boxShadow) {
            return UberPipeline::analytic;
        }
        return desc.kind() >= PrimitiveKind::glyphAlpha &&
                       desc.kind() <= PrimitiveKind::texturedQuad
                   ? UberPipeline::textured
                   : UberPipeline::sourceOver;
    case hal::BlendMode::screen:
        return UberPipeline::screen;
    case hal::BlendMode::additive:
        return UberPipeline::additive;
    case hal::BlendMode::replace:
        return UberPipeline::replace;
    case hal::BlendMode::multiply:
        return UberPipeline::multiply;
    }
    return UberPipeline::sourceOver;
}

void UberBatches::build(std::span<const PrimitiveDesc> primitives, bool earlyDepth) {
    opaque_.clear();
    ordered_.clear();
    runs_.clear();
    for (std::uint32_t i = static_cast<std::uint32_t>(primitives.size()); earlyDepth && i-- > 0;) {
        if (primitives[i].has(flags::opaque)) {
            opaque_.push_back(i);
        }
    }
    for (std::uint32_t i = 0; i < primitives.size(); ++i) {
        const UberPipeline pipeline = pipelineFor(primitives[i]);
        ordered_.push_back(i);
        if (!runs_.empty() && runs_.back().pipeline == pipeline) {
            ++runs_.back().count;
        } else {
            runs_.push_back({pipeline, i, 1});
        }
    }
}
}
