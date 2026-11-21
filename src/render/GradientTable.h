#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace prism::render {
enum class GradientSpace : std::uint8_t { linearRgb, oklab, oklch };

struct GradientStop {
    float position;
    std::uint32_t color;
};

class GradientTable {
  public:
    [[nodiscard]] std::uint32_t add(std::span<const GradientStop> stops, GradientSpace space);
    void clear() noexcept;

    [[nodiscard]] std::span<const float> rows() const noexcept { return rows_; }
    [[nodiscard]] std::uint32_t rowCount() const noexcept {
        return static_cast<std::uint32_t>(keys_.size());
    }

  private:
    std::vector<float> rows_;
    std::vector<std::uint64_t> keys_;

    void bake(std::span<const GradientStop> stops, GradientSpace space);
};
}
