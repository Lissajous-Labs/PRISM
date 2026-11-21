#pragma once

#include "render/DrawList.h"
#include "render/FrameView.h"
#include "telemetry/LoudnessMeter.h"

#include <complex>
#include <cstdint>
#include <span>
#include <vector>

namespace prism::widgets {
enum class FilterShape : std::uint8_t { lowpass, highpass, bandpass, peaking, lowShelf, highShelf };

[[nodiscard]] telemetry::Biquad designBiquad(FilterShape shape, double sampleRate, double frequency,
                                             double q, double gainDb = 0);

class FilterResponse {
  public:
    static constexpr std::size_t maxSections = 16;

    explicit FilterResponse(double sampleRate) : sampleRate_(sampleRate) {}

    bool add(const telemetry::Biquad &section);
    void clear() noexcept { sections_.clear(); }
    [[nodiscard]] std::complex<double> response(double frequency) const noexcept;
    [[nodiscard]] double magnitudeDb(double frequency) const noexcept;
    [[nodiscard]] double phase(double frequency) const noexcept {
        return std::arg(response(frequency));
    }

  private:
    double sampleRate_;
    std::vector<telemetry::Biquad> sections_;
};

class GpuFilterCurve {
  public:
    explicit GpuFilterCurve(std::uint32_t columns)
        : columns_(columns), strip_(static_cast<std::size_t>(columns) * 2) {}

    void build(const FilterResponse &response, double lowFrequency, double highFrequency,
               double minDb, double maxDb);
    [[nodiscard]] render::TextureView strip() const noexcept { return {columns_, 1, 2, strip_}; }
    [[nodiscard]] std::span<const float> bands() const noexcept { return strip_; }
    void draw(render::DrawList &list, render::RectF rect, std::uint32_t slot, float thickness,
              std::uint32_t color) const {
        list.plotStrip(rect, slot, columns_, thickness, color);
    }

  private:
    std::uint32_t columns_;
    std::vector<float> strip_;
};

}
