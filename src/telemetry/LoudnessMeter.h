#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace prism::telemetry {
struct Biquad {
    double b0 = 1;
    double b1 = 0;
    double b2 = 0;
    double a1 = 0;
    double a2 = 0;

    [[nodiscard]] static Biquad kWeightingShelf(double sampleRate) noexcept;
    [[nodiscard]] static Biquad kWeightingHighpass(double sampleRate) noexcept;
};

struct BiquadState {
    double z1 = 0;
    double z2 = 0;

    double process(const Biquad &f, double x) noexcept {
        const double y = f.b0 * x + z1;
        z1 = f.b1 * x - f.a1 * y + z2;
        z2 = f.b2 * x - f.a2 * y;
        return y;
    }
};

struct LoudnessReading {
    float momentary;
    float shortTerm;
    float integrated;
    float truePeak;
    float rmsLeft;
    float rmsRight;
};

inline constexpr float silence = -144.0f;

class LoudnessMeter {
  public:
    static constexpr std::size_t oversampling = 4;
    static constexpr std::size_t tapsPerPhase = 12;
    static constexpr std::size_t histogramBins = 8000;

    explicit LoudnessMeter(double sampleRate);

    void process(const float *left, const float *right, std::size_t frames) noexcept;
    [[nodiscard]] LoudnessReading reading() const noexcept;
    void reset() noexcept;

  private:
    std::size_t subBlock_;
    Biquad shelf_;
    Biquad highpass_;
    std::array<BiquadState, 2> shelfState_{};
    std::array<BiquadState, 2> highpassState_{};
    std::array<double, oversampling * tapsPerPhase> taps_{};
    std::array<std::array<float, tapsPerPhase>, 2> history_{};
    std::size_t historyIndex_ = 0;
    std::size_t subBlockFill_ = 0;
    std::array<double, 2> subWeighted_{};
    std::array<double, 2> subPlain_{};
    std::array<std::array<double, 30>, 2> weightedRing_{};
    std::array<std::array<double, 4>, 2> plainRing_{};
    std::size_t ringCount_ = 0;
    std::size_t ringIndex_ = 0;
    std::vector<std::uint64_t> histogramCount_;
    std::vector<double> histogramEnergy_;
    double peak_ = 0;
    std::array<std::atomic<float>, 6> published_{};

    void completeSubBlock() noexcept;
    [[nodiscard]] double windowEnergy(std::size_t subBlocks) const noexcept;
    [[nodiscard]] double integratedLoudness() const noexcept;
    [[nodiscard]] float truePeakSample(std::size_t channel, float sample) noexcept;
};

}
