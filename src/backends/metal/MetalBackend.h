#pragma once

#include "backends/metal/MetalSpectrogram.h"
#include "backends/metal/MetalTelemetry.h"
#include "render/RenderSurface.h"
#include "render/UberBatches.h"

#include <cstdint>
#include <memory>

namespace prism::backends {
struct MetalDeviceState;

struct ShadowQuery {
    float point[2];
    float half[2];
    float radius;
    float sigma;
    float reserved[2];
};
struct MetalSurfaceState;

class MetalSurface final : public render::RenderSurface {
  public:
    MetalSurface(std::shared_ptr<MetalDeviceState> device, hal::SurfaceDesc desc);
    ~MetalSurface() override;

    [[nodiscard]] hal::Extent extent() const noexcept override;
    void resize(hal::Extent extent) override;
    void loseDevice() noexcept override;
    [[nodiscard]] bool rebuild() override;
    [[nodiscard]] bool lost() const noexcept override;
    [[nodiscard]] bool render(const render::FrameRequest &request) override;
    [[nodiscard]] bool readback(std::span<float> rgba) override;
    [[nodiscard]] bool readbackPresented(std::span<std::uint8_t> rgba,
                                         std::uint32_t frame) override;

    [[nodiscard]] bool renderToLayer(const render::FrameRequest &request, void *layer,
                                     double displayPeriod, bool &presented);
    void waitIdle() override;
    void setEarlyDepth(bool enabled) noexcept;
    void setFragmentCounting(bool enabled) noexcept;
    [[nodiscard]] std::uint32_t lastFragmentCount() const noexcept;
    [[nodiscard]] double lastGpuMilliseconds() const noexcept;
    [[nodiscard]] std::uint32_t lastDrawCalls() const noexcept;
    [[nodiscard]] std::size_t lastUploadBytes() const noexcept;
    [[nodiscard]] bool lastSubmitted() const noexcept;

  private:
    bool submit(const render::FrameRequest &request, void *layer, double displayPeriod,
                bool &presented);
    std::unique_ptr<MetalSurfaceState> state_;
};

class MetalDevice final : public render::RenderDevice {
  public:
    explicit MetalDevice(std::shared_ptr<MetalDeviceState> state);
    ~MetalDevice() override;

    [[nodiscard]] hal::BackendKind kind() const noexcept override {
        return hal::BackendKind::metal;
    }
    [[nodiscard]] std::unique_ptr<render::RenderSurface>
    createRenderSurface(const hal::SurfaceDesc &desc) override;
    [[nodiscard]] std::unique_ptr<MetalSpectrogram> createSpectrogram(math::WavetableView table,
                                                                      std::uint32_t rows = 64);
    [[nodiscard]] std::unique_ptr<MetalTelemetry> createTelemetry(std::uint32_t scopeColumns);
    [[nodiscard]] bool virtualized() const;
    [[nodiscard]] bool reflectLayout(std::span<std::uint32_t, 16> output) const;
    [[nodiscard]] bool evaluateShadows(std::span<const ShadowQuery> queries,
                                       std::span<float> results) const;

    static std::unique_ptr<hal::BackendDevice> create();

  private:
    std::shared_ptr<MetalDeviceState> state_;
};
}
