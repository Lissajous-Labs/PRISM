#include "render/vector/PathTiler.h"

#include <algorithm>

namespace prism::render {
void PathTiler::run(std::span<const LineSegment> segments, std::uint32_t width,
                    std::uint32_t height) {
    grid_ = TileGrid::covering(width, height);
    countTiles(segments);
    scan();
    scatter(segments);
}

void PathTiler::countTiles(std::span<const LineSegment> segments) {
    counts_.assign(grid_.count(), 0);
    rowDeltas_.assign(static_cast<std::size_t>(grid_.rows) * (grid_.columns + 1), 0);
    for (const LineSegment &segment : segments) {
        const auto count = [&](std::uint32_t column, std::uint32_t row, PackedPiece) {
            ++counts_[static_cast<std::size_t>(row) * grid_.columns + column];
        };
        forEachTilePiece(segment, grid_, count);
        forEachBoundaryStep(segment, grid_, count);
        forEachRowCrossing(segment, grid_, [&](std::uint32_t row, std::uint32_t slot, int sign) {
            rowDeltas_[static_cast<std::size_t>(row) * (grid_.columns + 1) + slot] += sign;
        });
    }
}

void PathTiler::scan() {
    offsets_.assign(grid_.count() + 1, 0);
    for (std::size_t tile = 0; tile < counts_.size(); ++tile) {
        offsets_[tile + 1] = offsets_[tile] + counts_[tile];
    }
    backdrops_.assign(grid_.count(), 0);
    for (std::uint32_t row = 0; row < grid_.rows; ++row) {
        std::int32_t running = 0;
        for (std::uint32_t column = 0; column < grid_.columns; ++column) {
            running += rowDeltas_[static_cast<std::size_t>(row) * (grid_.columns + 1) + column];
            backdrops_[static_cast<std::size_t>(row) * grid_.columns + column] = running;
        }
    }
}

void PathTiler::scatter(std::span<const LineSegment> segments) {
    pieces_.resize(offsets_.back());
    std::fill(counts_.begin(), counts_.end(), 0);
    for (const LineSegment &segment : segments) {
        const auto write = [&](std::uint32_t column, std::uint32_t row, PackedPiece piece) {
            const std::size_t tile = static_cast<std::size_t>(row) * grid_.columns + column;
            pieces_[offsets_[tile] + counts_[tile]++] = piece;
        };
        forEachTilePiece(segment, grid_, write);
        forEachBoundaryStep(segment, grid_, write);
    }
}
}
