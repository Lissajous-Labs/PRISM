#pragma once

#include "hal/DamageRegion.h"

#include <array>
#include <cstdint>

namespace prism::hal {
class BufferAgeHistory {
  public:
    static constexpr std::uint32_t depth = 4;

    void push(const DamageRegion &frameDamage) {
        head_ = (head_ + 1) % depth;
        history_[head_].clear();
        history_[head_].apply(RegionOp::unite, frameDamage);
        if (count_ < depth) {
            ++count_;
        }
    }

    void repairRegion(std::uint32_t bufferAge, Rect viewport, DamageRegion &output) const {
        if (bufferAge == 0 || bufferAge > depth || bufferAge > count_) {
            output.assign(viewport);
            return;
        }
        output.clear();
        for (std::uint32_t age = 0; age < bufferAge; ++age) {
            output.apply(RegionOp::unite, history_[(head_ + depth - age) % depth]);
        }
        output.clip(viewport);
    }

  private:
    std::array<DamageRegion, depth> history_{};
    std::uint32_t head_ = depth - 1;
    std::uint32_t count_ = 0;
};
}
