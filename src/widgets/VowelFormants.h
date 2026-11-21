#pragma once

#include "widgets/GpuFilterCurve.h"

#include <array>

namespace prism::widgets {
class VowelFormants {
  public:
    explicit VowelFormants(double sampleRate = 48000) : response_(sampleRate) {
        response_.setParallel(true);
    }
    void configure(double vowel, double shiftOctaves, double resonance, double drive,
                   unsigned sections = 5) noexcept;
    void configureFormantComb(double vowel, double shiftOctaves, double resonance,
                              double drive) noexcept;
    [[nodiscard]] const FilterResponse &response() const noexcept { return response_; }
    [[nodiscard]] const std::array<double, 5> &frequencies() const noexcept { return frequencies_; }

  private:
    FilterResponse response_;
    std::array<double, 5> frequencies_{};
};
}
