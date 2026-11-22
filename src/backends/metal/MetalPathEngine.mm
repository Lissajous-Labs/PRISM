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
                              const render::FrameView &frame, hal::Extent extent,
                              id<MTLBuffer> transforms, id<MTLBuffer> clips) {
    if (frame.paths.empty()) {
        return;
    }
    grid_ = render::TileGrid::covering(extent.width, extent.height);
    if (buffers_.size() < frame.paths.size()) {
        buffers_.resize(frame.paths.size());
    }
    id<MTLDevice> gpu = device.device;
    batchRecords_.resize(frame.paths.size());
    std::uint32_t maxSegments = 1, maxColumns = 1, maxRows = 1, maxPartitions = 1;
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
        upload(gpu, b.coverage, nullptr, tiles * tileSize * tileSize * sizeof(std::uint16_t));
        auto uniforms = uniformsFor(b, draw);
        uniforms.depth = 1.0f - (static_cast<float>(draw.insertAfter) + 0.5f) /
                                    static_cast<float>(frame.primitives.size() + 1);
        auto &record = batchRecords_[i];
        std::memcpy(record.uniforms.data(), &uniforms, sizeof(uniforms));
        record.buffers = {b.segments.gpuAddress,  b.counts.gpuAddress,
                          b.rowDeltas.gpuAddress, b.offsets.gpuAddress,
                          b.scanState.gpuAddress, b.backdrops.gpuAddress,
                          b.cursors.gpuAddress,   b.pieces.gpuAddress,
                          b.coverage.gpuAddress,  0};
        maxSegments = std::max(maxSegments, draw.segmentCount);
        maxRows = std::max(maxRows, b.rows);
        maxColumns = std::max(maxColumns, b.columns);
        maxPartitions =
            std::max(maxPartitions, (uniforms.tileCount + scanPartition - 1) / scanPartition);
    }
    upload(gpu, batchBuffer_, batchRecords_.data(), batchRecords_.size() * sizeof(MetalPathBatch));
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
    [encoder setBuffer:batchBuffer_ offset:0 atIndex:0];
    for (std::size_t i = 0; i < frame.paths.size(); ++i) {
        const auto &b = buffers_[i];
        [encoder useResource:b.segments usage:MTLResourceUsageRead];
        for (id<MTLBuffer> buffer : {b.counts, b.rowDeltas, b.offsets, b.scanState, b.backdrops,
                                     b.cursors, b.pieces, b.coverage}) {
            [encoder useResource:buffer usage:MTLResourceUsageRead | MTLResourceUsageWrite];
        }
    }
    const auto paths = frame.paths.size();
    [encoder setComputePipelineState:device.pathBinCount];
    [encoder dispatchThreads:MTLSizeMake(maxSegments, paths, 1)
        threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    [encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
    [encoder setComputePipelineState:device.pathBatchScan];
    [encoder dispatchThreadgroups:MTLSizeMake(maxPartitions, paths, 1)
            threadsPerThreadgroup:MTLSizeMake(scanPartition, 1, 1)];
    [encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
    [encoder setComputePipelineState:device.pathScatter];
    [encoder dispatchThreads:MTLSizeMake(maxSegments, paths, 1)
        threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    [encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
    [encoder setComputePipelineState:device.pathFine];
    [encoder setBuffer:transforms offset:0 atIndex:1];
    [encoder setBuffer:clips offset:0 atIndex:2];
    [encoder dispatchThreadgroups:MTLSizeMake(maxColumns, maxRows, paths)
            threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
    [encoder endEncoding];
}

void MetalPathEngine::composite(MetalDeviceState &device, id<MTLRenderCommandEncoder> encoder,
                                std::size_t path) {
    [encoder setRenderPipelineState:device.pathComposite];
    [encoder setDepthStencilState:device.depthTest];
    [encoder setVertexBuffer:batchBuffer_ offset:0 atIndex:0];
    [encoder setFragmentBuffer:batchBuffer_ offset:0 atIndex:0];
    [encoder useResource:buffers_[path].coverage
                   usage:MTLResourceUsageRead
                  stages:MTLRenderStageFragment];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:6
              instanceCount:1
               baseInstance:path];
}
}
