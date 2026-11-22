#pragma once

#include "render/PrimitiveDesc.h"

#include <cstdint>
#include <span>
#include <vector>

namespace prism::render {
enum class UberPipeline : std::uint8_t {
    sourceOver,
    screen,
    additive,
    replace,
    multiply,
    subpixel,
    analytic,
    textured,
    compact
};

[[nodiscard]] constexpr bool blendsSourceOver(UberPipeline pipeline) noexcept {
    return pipeline == UberPipeline::sourceOver || pipeline == UberPipeline::analytic ||
           pipeline == UberPipeline::textured || pipeline == UberPipeline::compact;
}

struct DrawRun {
    UberPipeline pipeline;
    std::uint32_t first;
    std::uint32_t count;
};

class UberBatches {
  public:
    void build(std::span<const PrimitiveDesc> primitives, bool earlyDepth);

    [[nodiscard]] std::span<const std::uint32_t> opaqueIndices() const noexcept { return opaque_; }
    [[nodiscard]] std::span<const DrawRun> runs() const noexcept { return runs_; }

  private:
    std::vector<std::uint32_t> opaque_;
    std::vector<DrawRun> runs_;
};

[[nodiscard]] UberPipeline pipelineFor(const PrimitiveDesc &desc) noexcept;
}
