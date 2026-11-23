#pragma once

#include "render/FrameView.h"
#include "telemetry/SpectrumEstimator.h"
#include "telemetry/UmaTelemetry.h"

#include <cstdint>
#include <memory>
#include <span>

namespace prism::backends {
struct MetalDeviceState;

struct ScopeRequest {
    std::uint32_t channel = 0;
    std::uint32_t window = 2048;
    float gain = 1;
};

class MetalTelemetry {
  public:
    MetalTelemetry(std::shared_ptr<MetalDeviceState> device, std::uint32_t scopeColumns);
    ~MetalTelemetry();

    MetalTelemetry(const MetalTelemetry &) = delete;
    MetalTelemetry &operator=(const MetalTelemetry &) = delete;

    [[nodiscard]] telemetry::UmaLayout &layout() noexcept;
    [[nodiscard]] bool prepareScope(const ScopeRequest &request) noexcept;
    [[nodiscard]] bool dispatch();
    [[nodiscard]] render::TextureView scope() const noexcept;
    [[nodiscard]] const telemetry::UmaSnapshot &lastSnapshot() const noexcept;

    bool enableSpectrum(const telemetry::SpectrumSettings &settings);
    [[nodiscard]] bool prepareSpectrum() noexcept;
    [[nodiscard]] std::span<const float> spectrumPower() const noexcept;
    [[nodiscard]] std::span<const float> spectrumDensity(std::size_t band) const noexcept;
    [[nodiscard]] std::uint64_t spectrumGeneration() const noexcept;

  private:
    friend struct MetalSurfaceState;
    bool encode(void *commandBuffer);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
