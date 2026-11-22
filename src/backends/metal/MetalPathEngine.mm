#include "backends/metal/MetalPathEngine.h"
#include "backends/metal/MetalUpload.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace prism::backends {
namespace {
struct PathUniforms {
    std::uint32_t columns;
    std::uint32_t rows;
    std::uint32_t segmentCount;
    std::uint32_t tileCount;
    std::uint32_t color;
    std::uint32_t fillRule;
    float depth;
    std::uint32_t clipId;
    std::int32_t damage[4];
    std::int32_t origin[4];
};

static_assert(sizeof(PathUniforms) == 64);

constexpr std::uint32_t scanPartition = 256;
constexpr std::int32_t tileSize = static_cast<std::int32_t>(render::pathTileSize);

std::size_t pieceBound(std::span<const render::LineSegment> segments, std::uint32_t columns,
                       std::uint32_t rows) {
    std::size_t total = 0;
    const double tile = static_cast<double>(render::pathTileSize);
    for (const auto &s : segments) {
        const double spanRows =
            std::floor(std::max(s.y0, s.y1) / tile) - std::floor(std::min(s.y0, s.y1) / tile) + 1;
        const double spanColumns =
            std::floor(std::max(s.x0, s.x1) / tile) - std::floor(std::min(s.x0, s.x1) / tile) + 1;
        total +=
            static_cast<std::size_t>(std::min(spanRows, static_cast<double>(rows)) +
                                     2 * std::min(spanColumns, static_cast<double>(columns)) + 2);
    }
    return total;
}

void zero(id<MTLBlitCommandEncoder> blit, id<MTLBuffer> buffer, std::size_t bytes) {
    [blit fillBuffer:buffer range:NSMakeRange(0, std::max<std::size_t>(bytes, 4)) value:0];
}

PathUniforms uniformsFor(const MetalPathBuffers &b, const render::PathDraw &draw) {
    return {b.columns,
            b.rows,
            draw.segmentCount,
            b.columns * b.rows,
            draw.color,
            static_cast<std::uint32_t>(draw.rule),
            0,
            draw.clipId,
            {0, 0, 0, 0},
            {static_cast<std::int32_t>(b.originX), static_cast<std::int32_t>(b.originY), 0, 0}};
}
}

void MetalPathEngine::prepare(MetalDeviceState &device, id<MTLCommandBuffer> commands,
                              const render::FrameView &frame, hal::Extent extent) {
    if (frame.paths.empty()) {
        return;
    }
    grid_ = render::TileGrid::covering(extent.width, extent.height);
    if (buffers_.size() < frame.paths.size()) {
        buffers_.resize(frame.paths.size());
    }
    id<MTLDevice> gpu = device.device;
    for (std::size_t i = 0; i < frame.paths.size(); ++i) {
        const auto &draw = frame.paths[i];
        const auto segments = frame.pathSegments.subspan(draw.firstSegment, draw.segmentCount);
        MetalPathBuffers &b = buffers_[i];
        float minX = std::numeric_limits<float>::infinity(), minY = minX;
        float maxX = -minX, maxY = -minX;
        for (const auto &s : segments) {
            minX = std::min({minX, s.x0, s.x1});
            minY = std::min({minY, s.y0, s.y1});
            maxX = std::max({maxX, s.x0, s.x1});
            maxY = std::max({maxY, s.y0, s.y1});
        }
        const auto clampTile = [](float value, std::uint32_t limit, bool upper) {
            const double tile = std::floor(static_cast<double>(value) / tileSize) + (upper ? 1 : 0);
            return static_cast<std::uint32_t>(std::clamp(tile, 0.0, static_cast<double>(limit)));
        };
        if (segments.empty()) {
            minX = minY = maxX = maxY = 0;
        }
        b.originX = clampTile(minX, grid_.columns, false);
        b.originY = clampTile(minY, grid_.rows, false);
        b.columns = std::max(clampTile(maxX, grid_.columns, true), b.originX + 1) - b.originX;
        b.rows = std::max(clampTile(maxY, grid_.rows, true), b.originY + 1) - b.originY;
        const std::size_t tiles = static_cast<std::size_t>(b.columns) * b.rows;
        upload(gpu, b.segments, segments.data(), segments.size_bytes());
        upload(gpu, b.counts, nullptr, tiles * 4);
        upload(gpu, b.rowDeltas, nullptr, (b.columns + 1) * b.rows * 4);
        upload(gpu, b.offsets, nullptr, (tiles + 1) * 4);
        upload(gpu, b.backdrops, nullptr, tiles * 4);
        upload(gpu, b.cursors, nullptr, tiles * 4);
        const std::size_t pieces = pieceBound(segments, b.columns, b.rows);
        if (pieces > (std::numeric_limits<std::uint32_t>::max() >> 2)) {
            throw std::length_error("vector path exceeds scan descriptor capacity");
        }

        upload(gpu, b.pieces, nullptr, pieces * 8);
        upload(gpu, b.scanState, nullptr, ((tiles + scanPartition - 1) / scanPartition + 1) * 4);
    }
    id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
    for (std::size_t i = 0; i < frame.paths.size(); ++i) {
        const MetalPathBuffers &b = buffers_[i];
        const std::size_t tiles = static_cast<std::size_t>(b.columns) * b.rows;
        zero(blit, b.counts, tiles * 4);
        zero(blit, b.rowDeltas, (b.columns + 1) * b.rows * 4);
        zero(blit, b.cursors, tiles * 4);
        zero(blit, b.scanState, ((tiles + scanPartition - 1) / scanPartition + 1) * 4);
    }
    [blit endEncoding];
    id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
    for (std::size_t i = 0; i < frame.paths.size(); ++i) {
        const auto &draw = frame.paths[i];
        const MetalPathBuffers &b = buffers_[i];
        const PathUniforms uniforms = uniformsFor(b, draw);
        [encoder setComputePipelineState:device.pathBinCount];
        [encoder setBuffer:b.segments offset:0 atIndex:0];
        [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:1];
        [encoder setBuffer:b.counts offset:0 atIndex:2];
        [encoder setBuffer:b.rowDeltas offset:0 atIndex:3];
        [encoder dispatchThreads:MTLSizeMake(std::max<std::uint32_t>(draw.segmentCount, 1), 1, 1)
            threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [encoder setComputePipelineState:device.pathScan];
        [encoder setBuffer:b.counts offset:0 atIndex:0];
        [encoder setBuffer:b.offsets offset:0 atIndex:1];
        [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:2];
        [encoder setBuffer:b.scanState offset:0 atIndex:3];
        [encoder dispatchThreadgroups:MTLSizeMake((uniforms.tileCount + scanPartition - 1) /
                                                      scanPartition,
                                                  1, 1)
                threadsPerThreadgroup:MTLSizeMake(scanPartition, 1, 1)];
        [encoder setComputePipelineState:device.pathBackdrop];
        [encoder setBuffer:b.rowDeltas offset:0 atIndex:0];
        [encoder setBuffer:b.backdrops offset:0 atIndex:1];
        [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:2];
        [encoder dispatchThreads:MTLSizeMake(b.rows, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder setComputePipelineState:device.pathScatter];
        [encoder setBuffer:b.segments offset:0 atIndex:0];
        [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:1];
        [encoder setBuffer:b.offsets offset:0 atIndex:2];
        [encoder setBuffer:b.cursors offset:0 atIndex:3];
        [encoder setBuffer:b.pieces offset:0 atIndex:4];
        [encoder dispatchThreads:MTLSizeMake(std::max<std::uint32_t>(draw.segmentCount, 1), 1, 1)
            threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    }
    [encoder endEncoding];
}

void MetalPathEngine::encodeFine(MetalDeviceState &device, id<MTLCommandBuffer> commands,
                                 std::size_t path, const render::FrameView &frame,
                                 std::span<const hal::Rect> damage,
                                 const PathFineTargets &targets) {
    const auto &draw = frame.paths[path];
    const MetalPathBuffers &b = buffers_[path];
    id<MTLComputeCommandEncoder> encoder = nil;
    for (const hal::Rect &rect : damage) {
        const auto first = [](std::int32_t value) { return value / tileSize; };
        const auto last = [](std::int32_t value) { return (value + tileSize - 1) / tileSize; };
        const auto x0 =
            std::max<std::int32_t>(first(rect.x0), static_cast<std::int32_t>(b.originX));
        const auto y0 =
            std::max<std::int32_t>(first(rect.y0), static_cast<std::int32_t>(b.originY));
        const auto x1 =
            std::min<std::int32_t>(last(rect.x1), static_cast<std::int32_t>(b.originX + b.columns));
        const auto y1 =
            std::min<std::int32_t>(last(rect.y1), static_cast<std::int32_t>(b.originY + b.rows));
        if (x1 <= x0 || y1 <= y0) {
            continue;
        }
        PathUniforms uniforms = uniformsFor(b, draw);
        uniforms.depth = 1.0f - (static_cast<float>(draw.insertAfter) + 0.5f) /
                                    static_cast<float>(targets.maxDepth + 1);
        uniforms.damage[0] = rect.x0;
        uniforms.damage[1] = rect.y0;
        uniforms.damage[2] = rect.x1;
        uniforms.damage[3] = rect.y1;
        uniforms.origin[2] = x0 - static_cast<std::int32_t>(b.originX);
        uniforms.origin[3] = y0 - static_cast<std::int32_t>(b.originY);
        if (encoder == nil) {
            encoder = [commands computeCommandEncoder];
            [encoder setComputePipelineState:device.pathFine];
            [encoder setBuffer:b.offsets offset:0 atIndex:0];
            [encoder setBuffer:b.backdrops offset:0 atIndex:1];
            [encoder setBuffer:b.pieces offset:0 atIndex:2];
            [encoder setBuffer:targets.transforms offset:0 atIndex:4];
            [encoder setBuffer:targets.clips offset:0 atIndex:5];
            [encoder setTexture:targets.color atIndex:0];
            [encoder setTexture:targets.depth atIndex:1];
        }
        [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:3];
        [encoder dispatchThreadgroups:MTLSizeMake(static_cast<NSUInteger>(x1 - x0),
                                                  static_cast<NSUInteger>(y1 - y0), 1)
                threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
    }
    [encoder endEncoding];
}
}
