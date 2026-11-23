#pragma once

#include <array>
#include <cstdint>

namespace prism::telemetry {
inline constexpr std::uint32_t halfBandTaps = 23;

[[nodiscard]] std::array<double, halfBandTaps> designHalfBand() noexcept;

class HalfBandDecimator {
  public:
    explicit HalfBandDecimator(const std::array<double, halfBandTaps> &taps) noexcept
        : taps_(taps) {}

    bool push(double value, double &output) noexcept {
        history_[position_] = value;
        history_[position_ + halfBandTaps] = value;
        position_ = position_ + 1 == halfBandTaps ? 0 : position_ + 1;
        odd_ = !odd_;
        if (odd_) {
            return false;
        }

        const double *window = history_.data() + position_;
        const auto &taps = taps_;
        double sum = taps[halfBandTaps / 2] * window[halfBandTaps / 2];
        for (std::uint32_t k = 0; k < halfBandTaps; k += 2) {
            sum += taps[k] * window[k];
        }
        output = sum;
        return true;
    }

  private:
    std::array<double, halfBandTaps> taps_;
    std::array<double, 2 * halfBandTaps> history_{};
    std::uint32_t position_ = 0;
    bool odd_ = false;
};
}
