#pragma once

#include "hal/DamageRegion.h"
#include "hal/HalTypes.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace prism::scene {
inline constexpr std::int32_t batchCellSize = 64;
inline constexpr std::uint32_t passKeyCount = 256;
inline constexpr std::uint32_t maxBatches = 0xFFFF;

struct Batch {
    std::uint16_t key;
    std::uint32_t count;
};

class SpatialBatcher {
  public:
    explicit SpatialBatcher(hal::Extent viewport, std::uint32_t batchReserve = 4096)
        : columns_((static_cast<std::int32_t>(viewport.width) + batchCellSize - 1) / batchCellSize),
          rows_((static_cast<std::int32_t>(viewport.height) + batchCellSize - 1) / batchCellSize),
          cells_(static_cast<std::size_t>(std::max(columns_, 1)) * std::max(rows_, 1), 0) {
        batches_.reserve(batchReserve);
    }

    void beginFrame() noexcept {
        ++generation_;
        if (generation_ == 0) {
            std::fill(cells_.begin(), cells_.end(), 0);
            keyStamps_.fill(0);
            generation_ = 1;
        }
        batches_.clear();
    }

    [[nodiscard]] std::uint32_t insert(hal::Rect bounds, std::uint16_t key) {
        const std::int32_t cx0 = std::clamp(floorDiv(bounds.x0), 0, columns_ - 1);
        const std::int32_t cy0 = std::clamp(floorDiv(bounds.y0), 0, rows_ - 1);
        const std::int32_t cx1 = std::clamp(floorDiv(bounds.x1 - 1), 0, columns_ - 1);
        const std::int32_t cy1 = std::clamp(floorDiv(bounds.y1 - 1), 0, rows_ - 1);
        std::uint32_t floor = 0;
        bool covered = false;
        for (std::int32_t y = cy0; y <= cy1; ++y) {
            for (std::int32_t x = cx0; x <= cx1; ++x) {
                const std::uint32_t cell = cells_[static_cast<std::size_t>(y) * columns_ + x];
                if ((cell >> 16) == generation_) {
                    floor = std::max(floor, cell & 0xFFFF);
                    covered = true;
                }
            }
        }
        const std::uint32_t slot = key % passKeyCount;
        std::uint32_t batch;
        if (keyStamps_[slot] == generation_ && batches_[lastBatch_[slot]].key == key &&
            (!covered || lastBatch_[slot] >= floor)) {
            batch = lastBatch_[slot];
        } else if (batches_.size() < maxBatches) {
            batch = static_cast<std::uint32_t>(batches_.size());
            batches_.push_back({key, 0});
            lastBatch_[slot] = batch;
            keyStamps_[slot] = generation_;
        } else {
            batch = static_cast<std::uint32_t>(batches_.size() - 1);
        }
        ++batches_[batch].count;
        const std::uint32_t stamped = (static_cast<std::uint32_t>(generation_) << 16) | batch;
        for (std::int32_t y = cy0; y <= cy1; ++y) {
            for (std::int32_t x = cx0; x <= cx1; ++x) {
                std::uint32_t &cell = cells_[static_cast<std::size_t>(y) * columns_ + x];
                if ((cell >> 16) != generation_ || (cell & 0xFFFF) < batch) {
                    cell = stamped;
                }
            }
        }
        return batch;
    }

    [[nodiscard]] const std::vector<Batch> &batches() const noexcept { return batches_; }
    [[nodiscard]] std::uint16_t generation() const noexcept { return generation_; }

  private:
    std::int32_t columns_;
    std::int32_t rows_;
    std::vector<std::uint32_t> cells_;
    std::vector<Batch> batches_;
    std::array<std::uint32_t, passKeyCount> lastBatch_{};
    std::array<std::uint16_t, passKeyCount> keyStamps_{};
    std::uint16_t generation_ = 0;

    static std::int32_t floorDiv(std::int32_t value) noexcept {
        return value >= 0 ? value / batchCellSize : -((-value + batchCellSize - 1) / batchCellSize);
    }
};
}
