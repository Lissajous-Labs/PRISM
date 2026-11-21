#pragma once

#include "render/DrawList.h"
#include "render/FormantCurve.h"
#include "render/FrameView.h"
#include "telemetry/LoudnessMeter.h"

#include <array>
#include <complex>
#include <cstdint>
#include <span>

namespace prism::widgets {
enum class FilterShape : std::uint8_t { lowpass, highpass, bandpass, peaking, lowShelf, highShelf };

[[nodiscard]] telemetry::Biquad designBiquad(FilterShape shape, double sampleRate, double frequency,
                                             double q, double gainDb = 0);

class FilterResponse {
  public:
    static constexpr std::size_t maxSections = 16;

    explicit FilterResponse(double sampleRate) : sampleRate_(sampleRate) {}

    bool add(const telemetry::Biquad &section);
    void clear() noexcept { count_ = 0; }
    void setParallel(bool value) noexcept { parallel_ = value; }
    [[nodiscard]] bool parallel() const noexcept { return parallel_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] std::span<const telemetry::Biquad> sections() const noexcept {
        return std::span(sections_).first(count_);
    }
    [[nodiscard]] std::complex<double> response(double frequency) const noexcept;
    [[nodiscard]] double magnitudeDb(double frequency) const noexcept;
    [[nodiscard]] double phase(double frequency) const noexcept {
        return std::arg(response(frequency));
    }

  private:
    double sampleRate_;
    std::array<telemetry::Biquad, maxSections> sections_{};
    std::size_t count_ = 0;
    bool parallel_ = false;
};

struct FilterCurveStyle {
    float haloSigma = 2.3f;
    float haloGain = 0.15f;
    float fillAlpha = 0.14f;
    float fillDecay = 3.0f;
    float zeroLineAlpha = 0.0f;
};

class GpuFilterCurve {
  public:
    void build(const FilterResponse &response, double lowFrequency, double highFrequency,
               double minDb, double maxDb);
    void draw(render::DrawList &list, render::RectF rect, float thickness, std::uint32_t color,
              const FilterCurveStyle &style = {}) const;

  private:
    render::FormantPayload payload_{};
    float dbRange_ = 48;
};

}
