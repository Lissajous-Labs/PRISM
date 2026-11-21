#pragma once

#include "render/vector/PathTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace prism::render {
inline constexpr std::uint32_t pathTileSize = 16;
inline constexpr float pieceScale = 65535.0f / static_cast<float>(pathTileSize);

struct PackedPiece {
    std::uint16_t x0;
    std::uint16_t y0;
    std::uint16_t x1;
    std::uint16_t y1;
};

static_assert(sizeof(PackedPiece) == 8);

struct TileGrid {
    std::uint32_t columns;
    std::uint32_t rows;

    [[nodiscard]] static constexpr TileGrid covering(std::uint32_t width,
                                                     std::uint32_t height) noexcept {
        return {(width + pathTileSize - 1) / pathTileSize,
                (height + pathTileSize - 1) / pathTileSize};
    }
    [[nodiscard]] constexpr std::uint32_t count() const noexcept { return columns * rows; }
};

[[nodiscard]] inline std::uint16_t quantize(float local) noexcept {
    return static_cast<std::uint16_t>(
        std::lrint(std::clamp(local, 0.0f, static_cast<float>(pathTileSize)) * pieceScale));
}

[[nodiscard]] inline float dequantize(std::uint16_t value) noexcept {
    return static_cast<float>(value) / pieceScale;
}

[[nodiscard]] inline PackedPiece pack(float x0, float y0, float x1, float y1) noexcept {
    return {quantize(x0), quantize(y0), quantize(x1), quantize(y1)};
}

template <class Visitor>
void forEachTilePiece(const LineSegment &segment, TileGrid grid, Visitor &&visit) {
    const float tile = static_cast<float>(pathTileSize);
    if (segment.y0 == segment.y1) {
        return;
    }
    const float yMin = std::min(segment.y0, segment.y1);
    const float yMax = std::max(segment.y0, segment.y1);
    const auto rowFirst = static_cast<std::int64_t>(std::floor(yMin / tile));
    const auto rowLast = static_cast<std::int64_t>(std::floor(yMax / tile));
    const float slope = (segment.x1 - segment.x0) / (segment.y1 - segment.y0);
    for (std::int64_t row = std::max<std::int64_t>(rowFirst, 0);
         row <= std::min<std::int64_t>(rowLast, grid.rows - 1); ++row) {
        const float bandTop = static_cast<float>(row) * tile;
        const float ya = std::max(yMin, bandTop);
        const float yb = std::min(yMax, bandTop + tile);
        if (!(yb > ya)) {
            continue;
        }
        const float xa = segment.x0 + (ya - segment.y0) * slope;
        const float xb = segment.x0 + (yb - segment.y0) * slope;
        const bool downward = segment.y1 > segment.y0;
        const float startX = downward ? xa : xb;
        const float startY = downward ? ya : yb;
        const float endX = downward ? xb : xa;
        const float endY = downward ? yb : ya;
        const float xLow = std::min(xa, xb);
        const float xHigh = std::max(xa, xb);
        const auto columnFirst = static_cast<std::int64_t>(std::floor(xLow / tile));
        const auto columnLast =
            xHigh > xLow ? static_cast<std::int64_t>(std::ceil(xHigh / tile)) - 1 : columnFirst;
        for (std::int64_t column = std::max<std::int64_t>(columnFirst, 0);
             column <= std::min<std::int64_t>(columnLast, grid.columns - 1); ++column) {
            const float left = static_cast<float>(column) * tile;
            float t0 = 0;
            float t1 = 1;
            const float dx = endX - startX;
            if (dx != 0) {
                const float ta = (left - startX) / dx;
                const float tb = (left + tile - startX) / dx;
                t0 = std::max(t0, std::min(ta, tb));
                t1 = std::min(t1, std::max(ta, tb));
            }
            if (!(t1 > t0)) {
                continue;
            }
            const float dy = endY - startY;
            visit(static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(row),
                  pack(startX + dx * t0 - left, startY + dy * t0 - bandTop, startX + dx * t1 - left,
                       startY + dy * t1 - bandTop));
        }
    }
}

template <class Visitor>
void forEachBoundaryStep(const LineSegment &segment, TileGrid grid, Visitor &&visit) {
    const float tile = static_cast<float>(pathTileSize);
    const float xLow = std::min(segment.x0, segment.x1);
    const float xHigh = std::max(segment.x0, segment.x1);
    const auto first =
        std::max<std::int64_t>(static_cast<std::int64_t>(std::floor(xLow / tile)) + 1, 0);
    const auto last = std::min<std::int64_t>(static_cast<std::int64_t>(std::floor(xHigh / tile)),
                                             grid.columns - 1);
    for (std::int64_t column = first; column <= last; ++column) {
        const float boundary = static_cast<float>(column) * tile;
        const bool crosses = (segment.x0 < boundary && boundary <= segment.x1) ||
                             (segment.x1 < boundary && boundary <= segment.x0);
        if (!crosses) {
            continue;
        }
        const float t = (boundary - segment.x0) / (segment.x1 - segment.x0);
        const float y = segment.y0 + (segment.y1 - segment.y0) * t;
        const auto row = static_cast<std::int64_t>(std::floor(y / tile));
        if (row < 0 || row >= grid.rows) {
            continue;
        }
        const float local = y - static_cast<float>(row) * tile;
        if (local <= 0) {
            continue;
        }
        const bool rightward = segment.x1 > segment.x0;
        visit(static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(row),
              rightward ? pack(0, tile, 0, local) : pack(0, local, 0, tile));
    }
}

template <class Visitor>
void forEachRowCrossing(const LineSegment &segment, TileGrid grid, Visitor &&visit) {
    const float tile = static_cast<float>(pathTileSize);
    if (segment.y0 == segment.y1) {
        return;
    }
    const float yMin = std::min(segment.y0, segment.y1);
    const float yMax = std::max(segment.y0, segment.y1);
    const auto first = std::max<std::int64_t>(static_cast<std::int64_t>(std::ceil(yMin / tile)), 0);
    const auto last = std::min<std::int64_t>(static_cast<std::int64_t>(std::ceil(yMax / tile)) - 1,
                                             grid.rows - 1);
    const int sign = segment.y1 > segment.y0 ? 1 : -1;
    for (std::int64_t row = first; row <= last; ++row) {
        const float y = static_cast<float>(row) * tile;
        const float x =
            segment.x0 + (segment.x1 - segment.x0) * (y - segment.y0) / (segment.y1 - segment.y0);
        const auto column = static_cast<std::int64_t>(std::floor(x / tile));
        if (column >= grid.columns) {
            continue;
        }
        visit(static_cast<std::uint32_t>(row),
              static_cast<std::uint32_t>(std::max<std::int64_t>(column, -1) + 1), sign);
    }
}
}
