#pragma once

#include "backends/metal/MetalBackend.h"
#include "render/RenderSurface.h"
#include "telemetry/FrameStatistics.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

namespace prism::bench {
inline constexpr hal::Extent benchExtent{2560, 1440};
inline constexpr double displayScale = 2;
inline constexpr int skipCode = 77;

struct BenchContext {
    std::unique_ptr<hal::BackendDevice> device;
    std::unique_ptr<render::RenderSurface> surface;
    [[nodiscard]] backends::MetalSurface &metal() const noexcept {
        return static_cast<backends::MetalSurface &>(*surface);
    }
    [[nodiscard]] backends::MetalDevice &metalDevice() const noexcept {
        return static_cast<backends::MetalDevice &>(*device);
    }
};

[[nodiscard]] std::optional<BenchContext> openContext(hal::Extent extent = benchExtent);

class Stopwatch {
  public:
    Stopwatch() noexcept : start_(std::chrono::steady_clock::now()) {}
    [[nodiscard]] double milliseconds() const noexcept {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_)
            .count();
    }

  private:
    std::chrono::steady_clock::time_point start_;
};

void countAllocations(bool enabled) noexcept;
[[nodiscard]] std::size_t allocationCount() noexcept;

class Report {
  public:
    explicit Report(std::string_view workload) : workload_(workload) {}

    void distribution(std::string_view name, telemetry::FrameStatistics &statistics);
    bool below(std::string_view name, double value, double limit);
    bool atLeast(std::string_view name, double value, double limit);
    [[nodiscard]] int finish() const;

  private:
    std::string_view workload_;
    bool pass_ = true;
};
}
