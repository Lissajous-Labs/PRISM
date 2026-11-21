#pragma once

#include "math/Ballistics.h"
#include "telemetry/Fft.h"
#include "telemetry/TelemetryRing.h"
#include "widgets/HalftoneStyle.h"

#include <array>
#include <complex>
#include <span>
#include <vector>

namespace prism::widgets {
class GpuSpectralMatrix {
  public:
    explicit GpuSpectralMatrix(std::uint32_t columns = 32, double sampleRate = 48000);
    [[nodiscard]] bool push(const float *samples, std::size_t count) noexcept {
        return audio_.push(samples, count);
    }
    void consume(double dt);
    void analyze(std::span<const float> block, double dt, double tiltDb = 3.5);
    void analyzePartials(std::span<const float> amplitudes, double fundamental, double dt,
                         double tiltDb = 3.5);
    void stop() noexcept;
    void stepNormalized(std::span<const float> bands, double dt) noexcept;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] double columnFrequency(std::uint32_t column) const noexcept;
    [[nodiscard]] std::span<const math::SpectralBallistics> states() const noexcept {
        return states_;
    }
    [[nodiscard]] render::TextureView bands() const noexcept {
        return {columns_, 1, 4, bands_, hal::PixelFormat::rgba16Float};
    }
    void draw(render::DrawList &list, render::RectF rect, std::uint32_t slot,
              std::uint32_t paletteRow, float rows = 28) const;

  private:
    static constexpr std::size_t fftSize = 4096;
    struct BinWeight {
        std::size_t bin;
        double weight;
    };
    struct Band {
        double frequency, lower, upper;
        std::size_t first, count;
    };
    std::vector<BinWeight> weights_;
    std::vector<Band> bank_;
    std::uint32_t columns_;
    double sampleRate_;
    telemetry::Fft fft_;
    telemetry::TelemetryRing<float, 16384> audio_;
    std::array<float, fftSize> latest_{};
    std::array<float, fftSize> incoming_{};
    std::size_t write_ = 0;
    std::array<double, fftSize> window_{};
    std::array<double, fftSize> input_{};
    std::array<std::complex<double>, fftSize / 2 + 1> spectrum_{};
    std::vector<float> instant_;
    std::vector<math::SpectralBallistics> states_;
    std::vector<float> bands_;
    double low_ = 20;
    double high_;
    void finishSpectrum(double dt, double tiltDb);
};
}
