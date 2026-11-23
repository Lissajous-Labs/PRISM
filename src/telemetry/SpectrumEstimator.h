#pragma once

#include "telemetry/Fft.h"
#include "telemetry/HalfBand.h"

#include <array>
#include <complex>
#include <cstdint>
#include <span>
#include <vector>

namespace prism::telemetry {
struct SpectrumBand {
    double low;
    double high;
    std::uint32_t decimation;
    std::uint32_t size;
};

inline constexpr std::array<SpectrumBand, 4> spectrumBands{
    {{20, 125, 8, 4096}, {125, 500, 4, 4096}, {500, 2000, 2, 4096}, {2000, 24000, 1, 512}}};
inline constexpr double crossoverOctaves = 1.0 / 6;

struct SpectrumSettings {
    double sampleRate = 48000;
    std::uint32_t knots = 256;
    double lowHz = 20;
    double highHz = 20000;
    double tiltDbPerOctave = 4.5;
    double attackSeconds = 0.012;
    double releaseSeconds = 0.220;
    double holdSeconds = 1.2;
    double peakFallDbPerSecond = 12;
    double floorDb = -140;
};

class SpectrumEstimator {
  public:
    explicit SpectrumEstimator(const SpectrumSettings &settings);

    void push(std::span<const float> samples) noexcept;
    bool analyze(double dt) noexcept;
    void smooth(double dt) noexcept;

    [[nodiscard]] std::span<const double> frequencies() const noexcept { return frequencies_; }
    [[nodiscard]] std::span<const double> power() const noexcept { return power_; }
    [[nodiscard]] std::span<const double> instant() const noexcept { return instant_; }
    [[nodiscard]] std::span<const double> levels() const noexcept { return levels_; }
    [[nodiscard]] std::span<const double> peaks() const noexcept { return peaks_; }
    [[nodiscard]] std::span<const double> slopes() const noexcept { return slopes_; }
    [[nodiscard]] double binWidth(std::size_t band) const noexcept;
    [[nodiscard]] const SpectrumSettings &settings() const noexcept { return settings_; }

  private:
    struct Band {
        double rate;
        std::vector<double> window;
        double windowPower;
        std::vector<double> history;
        std::size_t written = 0;
        std::vector<double> input;
        std::vector<std::complex<double>> spectrum;
        std::vector<double> density;
        Fft fft;
    };

    void feed(std::size_t band, double value) noexcept;
    void transform(Band &band) noexcept;
    [[nodiscard]] double average(const Band &band, double low, double high) const noexcept;
    [[nodiscard]] double weight(std::size_t band, double hz) const noexcept;

    SpectrumSettings settings_;
    std::array<double, halfBandTaps> taps_;
    std::array<HalfBandDecimator, 3> stages_;
    std::vector<Band> bands_;
    std::vector<double> frequencies_;
    std::vector<double> edges_;
    std::vector<double> positions_;
    std::vector<double> power_;
    std::vector<double> instant_;
    std::vector<double> levels_;
    std::vector<double> peaks_;
    std::vector<double> holds_;
    std::vector<double> slopes_;
    bool primed_ = false;
};
}
