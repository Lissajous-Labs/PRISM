#pragma once

#include <cstdint>

namespace prism::test {
class Pcg {
  public:
    __extension__ using State = unsigned __int128;

    constexpr explicit Pcg(std::uint64_t seed) noexcept
        : state_((State(seed) << 64) | 0x9E3779B97F4A7C15ULL) {
        next();
    }

    constexpr std::uint64_t next() noexcept {
        state_ = state_ * multiplier + increment;
        const auto high = static_cast<std::uint64_t>(state_ >> 64);
        const auto low = static_cast<std::uint64_t>(state_);
        const auto rotation = static_cast<unsigned>(high >> 58);
        const std::uint64_t mixed = high ^ low;
        return (mixed >> rotation) | (mixed << ((64 - rotation) & 63));
    }

    constexpr double unit() noexcept { return static_cast<double>(next() >> 11) * 0x1p-53; }
    constexpr float unitFloat() noexcept { return static_cast<float>(next() >> 40) * 0x1p-24f; }
    constexpr double range(double low, double high) noexcept { return low + (high - low) * unit(); }

  private:
    static constexpr State multiplier =
        (State(0x2360ED051FC65DA4ULL) << 64) | 0x4385DF649FCCF645ULL;
    static constexpr State increment = (State(0x5851F42D4C957F2DULL) << 64) | 0x14057B7EF767814FULL;
    State state_;
};
}
