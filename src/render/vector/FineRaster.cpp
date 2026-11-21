#include "render/vector/FineRaster.h"

namespace prism::render {
void rasterizeCoverage(const PathTiler &tiler, FillRule rule, std::uint32_t width,
                       std::uint32_t height, std::span<float> coverage) {
    const TileGrid grid = tiler.grid();
    const auto offsets = tiler.offsets();
    const auto pieces = tiler.pieces();
    for (std::uint32_t tileRow = 0; tileRow < grid.rows; ++tileRow) {
        for (std::uint32_t tileColumn = 0; tileColumn < grid.columns; ++tileColumn) {
            const std::size_t tile = static_cast<std::size_t>(tileRow) * grid.columns + tileColumn;
            const std::int32_t backdrop =
                tiler.backdrops()[tile] * static_cast<std::int32_t>(windingScale);
            for (std::uint32_t j = 0; j < pathTileSize; ++j) {
                const std::uint32_t y = tileRow * pathTileSize + j;
                if (y >= height) {
                    break;
                }
                for (std::uint32_t i = 0; i < pathTileSize; ++i) {
                    const std::uint32_t x = tileColumn * pathTileSize + i;
                    if (x >= width) {
                        break;
                    }
                    std::int32_t sum = backdrop;
                    for (std::uint32_t p = offsets[tile]; p < offsets[tile + 1]; ++p) {
                        sum += fixedContribution(pieces[p], i, j);
                    }
                    coverage[static_cast<std::size_t>(y) * width + x] =
                        windingToAlpha(static_cast<float>(sum) / windingScale, rule);
                }
            }
        }
    }
}
}
