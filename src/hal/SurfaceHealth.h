#pragma once

#include <cstdint>

namespace prism::hal {
enum class SurfaceHealth : std::uint8_t { ready, recovering };

class SurfaceHealthMachine {
  public:
    [[nodiscard]] SurfaceHealth state() const noexcept { return state_; }
    [[nodiscard]] std::uint32_t recoveries() const noexcept { return recoveries_; }

    void reportFault() noexcept { state_ = SurfaceHealth::recovering; }

    [[nodiscard]] bool completeRecovery() noexcept {
        if (state_ != SurfaceHealth::recovering) {
            return false;
        }
        state_ = SurfaceHealth::ready;
        ++recoveries_;
        return true;
    }

  private:
    SurfaceHealth state_ = SurfaceHealth::ready;
    std::uint32_t recoveries_ = 0;
};
}
