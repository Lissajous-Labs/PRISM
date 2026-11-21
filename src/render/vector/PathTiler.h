#pragma once

#include "render/vector/TileGeometry.h"

#include <cstdint>
#include <span>
#include <vector>

namespace prism::render {
class PathTiler {
  public:
    void run(std::span<const LineSegment> segments, std::uint32_t width, std::uint32_t height);

    [[nodiscard]] TileGrid grid() const noexcept { return grid_; }
    [[nodiscard]] std::span<const std::uint32_t> offsets() const noexcept { return offsets_; }
    [[nodiscard]] std::span<const std::int32_t> backdrops() const noexcept { return backdrops_; }
    [[nodiscard]] std::span<const PackedPiece> pieces() const noexcept { return pieces_; }

  private:
    TileGrid grid_{0, 0};
    std::vector<std::uint32_t> counts_;
    std::vector<std::uint32_t> offsets_;
    std::vector<std::int32_t> rowDeltas_;
    std::vector<std::int32_t> backdrops_;
    std::vector<PackedPiece> pieces_;

    void countTiles(std::span<const LineSegment> segments);
    void scan();
    void scatter(std::span<const LineSegment> segments);
};
}
