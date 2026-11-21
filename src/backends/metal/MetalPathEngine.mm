#include "backends/metal/MetalPathEngine.h"
#include "backends/metal/MetalUpload.h"

#include <cmath>

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
};

static_assert(sizeof(PathUniforms) == 48);

constexpr std::uint32_t scanPartition = 256;

std::size_t pieceBound(std::span<const render::LineSegment> segments, render::TileGrid grid) {
    std::size_t total = 0;
    const double tile = static_cast<double>(render::pathTileSize);
    for (const auto &s : segments) {
        const double rows =
            std::floor(std::max(s.y0, s.y1) / tile) - std::floor(std::min(s.y0, s.y1) / tile) + 1;
        const double columns =
            std::floor(std::max(s.x0, s.x1) / tile) - std::floor(std::min(s.x0, s.x1) / tile) + 1;
        total +=
            static_cast<std::size_t>(std::min(rows, static_cast<double>(grid.rows)) +
                                     2 * std::min(columns, static_cast<double>(grid.columns)) + 2);
    }
    return total;
}

void zero(id<MTLBlitCommandEncoder> blit, id<MTLBuffer> buffer, std::size_t bytes) {
    [blit fillBuffer:buffer range:NSMakeRange(0, std::max<std::size_t>(bytes, 4)) value:0];
}
}

void MetalPathEngine::prepare(MetalDeviceState &device, id<MTLCommandBuffer> commands,
                              const render::FrameView &frame, hal::Extent extent) {
    grid_ = render::TileGrid::covering(extent.width, extent.height);
    if (buffers_.size() < frame.paths.size()) {
        buffers_.resize(frame.paths.size());
    }
    const std::size_t tiles = grid_.count();
    id<MTLDevice> gpu = device.device;
    for (std::size_t i = 0; i < frame.paths.size(); ++i) {
        const auto &draw = frame.paths[i];
        const auto segments = frame.pathSegments.subspan(draw.firstSegment, draw.segmentCount);
        MetalPathBuffers &b = buffers_[i];
        upload(gpu, b.segments, segments.data(), segments.size_bytes());
        upload(gpu, b.counts, nullptr, tiles * 4);
        upload(gpu, b.rowDeltas, nullptr, (grid_.columns + 1) * grid_.rows * 4);
        upload(gpu, b.offsets, nullptr, (tiles + 1) * 4);
        upload(gpu, b.backdrops, nullptr, tiles * 4);
        upload(gpu, b.cursors, nullptr, tiles * 4);
        upload(gpu, b.pieces, nullptr, pieceBound(segments, grid_) * 8);
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        zero(blit, b.counts, tiles * 4);
        zero(blit, b.rowDeltas, (grid_.columns + 1) * grid_.rows * 4);
        zero(blit, b.cursors, tiles * 4);
        [blit endEncoding];
        const PathUniforms uniforms{grid_.columns,
                                    grid_.rows,
                                    draw.segmentCount,
                                    static_cast<std::uint32_t>(tiles),
                                    draw.color,
                                    static_cast<std::uint32_t>(draw.rule),
                                    0,
                                    draw.clipId,
                                    {0, 0, 0, 0}};
        id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
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
        [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1)
                threadsPerThreadgroup:MTLSizeMake(scanPartition, 1, 1)];
        [encoder setComputePipelineState:device.pathBackdrop];
        [encoder setBuffer:b.rowDeltas offset:0 atIndex:0];
        [encoder setBuffer:b.backdrops offset:0 atIndex:1];
        [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:2];
        [encoder dispatchThreads:MTLSizeMake(grid_.rows, 1, 1)
            threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
        [encoder setComputePipelineState:device.pathScatter];
        [encoder setBuffer:b.segments offset:0 atIndex:0];
        [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:1];
        [encoder setBuffer:b.offsets offset:0 atIndex:2];
        [encoder setBuffer:b.cursors offset:0 atIndex:3];
        [encoder setBuffer:b.pieces offset:0 atIndex:4];
        [encoder dispatchThreads:MTLSizeMake(std::max<std::uint32_t>(draw.segmentCount, 1), 1, 1)
            threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [encoder endEncoding];
    }
}

void MetalPathEngine::encodeFine(MetalDeviceState &device, id<MTLCommandBuffer> commands,
                                 std::size_t path, const render::FrameView &frame, hal::Rect damage,
                                 const PathFineTargets &targets) {
    const auto &draw = frame.paths[path];
    MetalPathBuffers &b = buffers_[path];
    const float depth = 1.0f - (static_cast<float>(draw.insertAfter) + 0.5f) /
                                   static_cast<float>(targets.maxDepth + 1);
    const PathUniforms uniforms{
        grid_.columns, grid_.rows,  draw.segmentCount,
        grid_.count(), draw.color,  static_cast<std::uint32_t>(draw.rule),
        depth,         draw.clipId, {damage.x0, damage.y0, damage.x1, damage.y1}};
    id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
    [encoder setComputePipelineState:device.pathFine];
    [encoder setBuffer:b.offsets offset:0 atIndex:0];
    [encoder setBuffer:b.backdrops offset:0 atIndex:1];
    [encoder setBuffer:b.pieces offset:0 atIndex:2];
    [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:3];
    [encoder setBuffer:targets.transforms offset:0 atIndex:4];
    [encoder setBuffer:targets.clips offset:0 atIndex:5];
    [encoder setTexture:targets.color atIndex:0];
    [encoder setTexture:targets.depth atIndex:1];
    [encoder dispatchThreadgroups:MTLSizeMake(grid_.columns, grid_.rows, 1)
            threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
    [encoder endEncoding];
}
}
