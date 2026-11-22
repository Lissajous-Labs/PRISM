constant float pathTile = 16.0f;
constant float pieceScale = 65535.0f / 16.0f;
constant float windingScale = 1048576.0f;
constant uint scanPartition = 256;

struct PathUniforms {
    uint columns;
    uint rows;
    uint segmentCount;
    uint tileCount;
    uint color;
    uint fillRule;
    float depth;
    uint clipId;
    int4 damage;
    int4 origin;
};

struct BatchPath {
    PathUniforms uniforms;
    device const float4 *segments;
    device atomic_uint *counts;
    device atomic_int *rowDeltas;
    device uint *offsets;
    device atomic_uint *scanState;
    device int *backdrops;
    device atomic_uint *cursors;
    device uint2 *pieces;
    device half *coverage;
    ulong reserved;
};

uint quantizeLocal(float value) {
    return uint(rint(clamp(value, 0.0f, pathTile) * pieceScale));
}

uint2 packPiece(float x0, float y0, float x1, float y1) {
    return uint2(quantizeLocal(x0) | (quantizeLocal(y0) << 16), quantizeLocal(x1) | (quantizeLocal(y1) << 16));
}

struct PieceSink {
    device atomic_uint *counts;
    device atomic_uint *cursors;
    device const uint *offsets;
    device uint2 *pieces;
    bool scatter;

    void emit(uint column, uint row, uint columns, uint2 piece) {
        uint tile = row * columns + column;
        if (scatter) {
            uint slot = atomic_fetch_add_explicit(&cursors[tile], 1u, memory_order_relaxed);
            pieces[offsets[tile] + slot] = piece;
        } else {
            atomic_fetch_add_explicit(&counts[tile], 1u, memory_order_relaxed);
        }
    }
};

void visitTilePieces(float4 s, constant PathUniforms &u, thread PieceSink &sink) {
    if (s.y == s.w) {
        return;
    }
    float yMin = min(s.y, s.w);
    float yMax = max(s.y, s.w);
    int rowFirst = max(int(floor(yMin / pathTile)), 0);
    int rowLast = min(int(floor(yMax / pathTile)), int(u.rows) - 1);
    float slope = (s.z - s.x) / (s.w - s.y);
    bool downward = s.w > s.y;
    for (int row = rowFirst; row <= rowLast; ++row) {
        float bandTop = float(row) * pathTile;
        float ya = max(yMin, bandTop);
        float yb = min(yMax, bandTop + pathTile);
        if (!(yb > ya)) {
            continue;
        }
        float xa = s.x + (ya - s.y) * slope;
        float xb = s.x + (yb - s.y) * slope;
        float startX = downward ? xa : xb;
        float startY = downward ? ya : yb;
        float endX = downward ? xb : xa;
        float endY = downward ? yb : ya;
        float xLow = min(xa, xb);
        float xHigh = max(xa, xb);
        int columnFirst = int(floor(xLow / pathTile));
        int columnLast = xHigh > xLow ? int(ceil(xHigh / pathTile)) - 1 : columnFirst;
        float dx = endX - startX;
        float dy = endY - startY;
        for (int column = max(columnFirst, 0); column <= min(columnLast, int(u.columns) - 1); ++column) {
            float left = float(column) * pathTile;
            float t0 = 0.0f;
            float t1 = 1.0f;
            if (dx != 0.0f) {
                float ta = (left - startX) / dx;
                float tb = (left + pathTile - startX) / dx;
                t0 = max(t0, min(ta, tb));
                t1 = min(t1, max(ta, tb));
            }
            if (!(t1 > t0)) {
                continue;
            }
            sink.emit(uint(column), uint(row), u.columns,
                      packPiece(startX + dx * t0 - left, startY + dy * t0 - bandTop, startX + dx * t1 - left,
                                startY + dy * t1 - bandTop));
        }
    }
}

void visitBoundarySteps(float4 s, constant PathUniforms &u, thread PieceSink &sink) {
    float xLow = min(s.x, s.z);
    float xHigh = max(s.x, s.z);
    int first = max(int(floor(xLow / pathTile)) + 1, 0);
    int last = min(int(floor(xHigh / pathTile)), int(u.columns) - 1);
    for (int column = first; column <= last; ++column) {
        float boundary = float(column) * pathTile;
        bool crosses = (s.x < boundary && boundary <= s.z) || (s.z < boundary && boundary <= s.x);
        if (!crosses) {
            continue;
        }
        float t = (boundary - s.x) / (s.z - s.x);
        float y = s.y + (s.w - s.y) * t;
        int row = int(floor(y / pathTile));
        if (row < 0 || row >= int(u.rows)) {
            continue;
        }
        float local = y - float(row) * pathTile;
        if (local <= 0.0f) {
            continue;
        }
        sink.emit(uint(column), uint(row), u.columns,
                  s.z > s.x ? packPiece(0.0f, pathTile, 0.0f, local) : packPiece(0.0f, local, 0.0f, pathTile));
    }
}

kernel void pathBinCount(constant BatchPath *paths [[buffer(0)]],
                         uint2 position [[thread_position_in_grid]]) {
    constant BatchPath &path = paths[position.y];
    constant PathUniforms &u = path.uniforms;
    uint index = position.x;
    device const float4 *segments = path.segments;
    device atomic_uint *counts = path.counts;
    device atomic_int *rowDeltas = path.rowDeltas;
    if (index >= u.segmentCount) {
        return;
    }
    float4 s = segments[index] - float4(float2(u.origin.xy), float2(u.origin.xy)) * pathTile;
    PieceSink sink;
    sink.counts = counts;
    sink.scatter = false;
    visitTilePieces(s, u, sink);
    visitBoundarySteps(s, u, sink);
    if (s.y == s.w) {
        return;
    }
    float yMin = min(s.y, s.w);
    float yMax = max(s.y, s.w);
    int first = max(int(ceil(yMin / pathTile)), 0);
    int last = min(int(ceil(yMax / pathTile)) - 1, int(u.rows) - 1);
    int sign = s.w > s.y ? 1 : -1;
    for (int row = first; row <= last; ++row) {
        float y = float(row) * pathTile;
        float x = s.x + (s.z - s.x) * (y - s.y) / (s.w - s.y);
        int column = int(floor(x / pathTile));
        if (column >= int(u.columns)) {
            continue;
        }
        atomic_fetch_add_explicit(&rowDeltas[uint(row) * (u.columns + 1) + uint(max(column, -1) + 1)], sign,
                                  memory_order_relaxed);
    }
}

void scanPath(device const uint *counts, device uint *offsets, constant PathUniforms &u,
              device atomic_uint *state, uint local, uint lane, uint simdGroup, uint simdWidth,
              threadgroup uint &partition, threadgroup uint *sums, threadgroup uint &prefix) {
    if (local == 0) {
        partition = atomic_fetch_add_explicit(&state[0], 1u, memory_order_relaxed);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    uint index = partition * scanPartition + local;
    uint value = index < u.tileCount ? counts[index] : 0u;
    uint exclusive = simd_prefix_exclusive_sum(value);
    uint total = simd_sum(value);
    if (lane == 0) {
        sums[simdGroup] = total;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    uint before = 0;
    for (uint group = 0; group < simdGroup; ++group) {
        before += sums[group];
    }
    if (local == 0) {
        uint aggregate = 0;
        for (uint group = 0; group < scanPartition / simdWidth; ++group) {
            aggregate += sums[group];
        }
        atomic_store_explicit(&state[partition + 1], (aggregate << 2) | 1u,
                              memory_order_relaxed);

        uint carry = 0;
        uint previous = partition;
        while (previous > 0) {
            uint descriptor = atomic_load_explicit(&state[previous], memory_order_relaxed);
            if ((descriptor & 3u) == 0u) {
                uint base = (previous - 1u) * scanPartition;
                for (uint item = base; item < min(base + scanPartition, u.tileCount); ++item) {
                    carry += counts[item];
                }
                --previous;
                continue;
            }
            carry += descriptor >> 2;
            if ((descriptor & 3u) == 2u) {
                break;
            }
            --previous;
        }
        prefix = carry;
        atomic_store_explicit(&state[partition + 1], ((carry + aggregate) << 2) | 2u,
                              memory_order_relaxed);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    if (index < u.tileCount) {
        offsets[index] = prefix + before + exclusive;
        if (index + 1 == u.tileCount) {
            offsets[index + 1] = prefix + before + exclusive + value;
        }
    }
}

kernel void pathScan(device const uint *counts [[buffer(0)]], device uint *offsets [[buffer(1)]],
                     constant PathUniforms &u [[buffer(2)]], device atomic_uint *state [[buffer(3)]],
                     uint local [[thread_position_in_threadgroup]],
                     uint lane [[thread_index_in_simdgroup]],
                     uint simdGroup [[simdgroup_index_in_threadgroup]],
                     uint simdWidth [[threads_per_simdgroup]]) {
    threadgroup uint partition;
    threadgroup uint sums[scanPartition];
    threadgroup uint prefix;
    scanPath(counts, offsets, u, state, local, lane, simdGroup, simdWidth, partition, sums, prefix);
}

kernel void pathBatchScan(constant BatchPath *paths [[buffer(0)]],
                          uint2 group [[threadgroup_position_in_grid]],
                          uint2 local [[thread_position_in_threadgroup]],
                          uint lane [[thread_index_in_simdgroup]],
                          uint simdGroup [[simdgroup_index_in_threadgroup]],
                          uint simdWidth [[threads_per_simdgroup]]) {
    constant BatchPath &path = paths[group.y];
    if (group.x >= (path.uniforms.tileCount + scanPartition - 1) / scanPartition) {
        return;
    }
    uint row = group.x * scanPartition + local.x;
    if (row < path.uniforms.rows) {
        device const int *deltas = reinterpret_cast<device const int *>(path.rowDeltas);
        int running = 0;
        for (uint column = 0; column < path.uniforms.columns; ++column) {
            running += deltas[row * (path.uniforms.columns + 1) + column];
            path.backdrops[row * path.uniforms.columns + column] = running;
        }
    }
    threadgroup uint partition;
    threadgroup uint sums[scanPartition];
    threadgroup uint prefix;
    scanPath(reinterpret_cast<device const uint *>(path.counts), path.offsets, path.uniforms,
             path.scanState, local.x, lane, simdGroup, simdWidth, partition, sums, prefix);
}


kernel void pathScatter(constant BatchPath *paths [[buffer(0)]],
                        uint2 position [[thread_position_in_grid]]) {
    constant BatchPath &path = paths[position.y];
    constant PathUniforms &u = path.uniforms;
    uint index = position.x;
    device const float4 *segments = path.segments;
    device const uint *offsets = path.offsets;
    device atomic_uint *cursors = path.cursors;
    device uint2 *pieces = path.pieces;
    if (index >= u.segmentCount) {
        return;
    }
    PieceSink sink;
    sink.cursors = cursors;
    sink.offsets = offsets;
    sink.pieces = pieces;
    sink.scatter = true;
    float4 s = segments[index] - float4(float2(u.origin.xy), float2(u.origin.xy)) * pathTile;
    visitTilePieces(s, u, sink);
    visitBoundarySteps(s, u, sink);
}

float coverageAntiderivative(float value) {
    return value <= 0.0f ? value : (value >= 1.0f ? 0.5f : value - 0.5f * value * value);
}

float coverageWeight(float value) {
    return value <= 0.0f ? 1.0f : (value >= 1.0f ? 0.0f : 1.0f - value);
}

int fixedContribution(uint2 packed, uint column, uint row) {
    float x0 = float(packed.x & 0xFFFFu) / pieceScale;
    float y0 = float(packed.x >> 16) / pieceScale;
    float x1 = float(packed.y & 0xFFFFu) / pieceScale;
    float y1 = float(packed.y >> 16) / pieceScale;
    float top = float(row);
    float ya = max(min(y0, y1), top);
    float yb = min(max(y0, y1), top + 1.0f);
    if (!(yb > ya) || y0 == y1) {
        return 0;
    }
    float slope = (x1 - x0) / (y1 - y0);
    float xa = x0 + (ya - y0) * slope - float(column);
    float xb = x0 + (yb - y0) * slope - float(column);
    float dy = (yb - ya) * (y1 > y0 ? 1.0f : -1.0f);
    float average = abs(xb - xa) > 1e-5f ? (coverageAntiderivative(xb) - coverageAntiderivative(xa)) / (xb - xa)
                                         : coverageWeight(0.5f * (xa + xb));
    return int(rint(dy * average * windingScale));
}

float windingToAlpha(float winding, uint rule) {
    float magnitude = abs(winding);
    return rule == 0u ? min(magnitude, 1.0f) : 1.0f - abs(fmod(magnitude, 2.0f) - 1.0f);
}

kernel void pathFine(constant BatchPath *paths [[buffer(0)]],
                     device const TransformRecord *transforms [[buffer(1)]],
                     device const ClipRecord *clips [[buffer(2)]],
                     uint3 tileIndex [[threadgroup_position_in_grid]],
                     uint3 local [[thread_position_in_threadgroup]]) {
    constant BatchPath &path = paths[tileIndex.z];
    constant PathUniforms &u = path.uniforms;
    if (tileIndex.x >= u.columns || tileIndex.y >= u.rows) {
        return;
    }
    threadgroup uint2 shared[256];
    uint tile = tileIndex.y * u.columns + tileIndex.x;
    uint first = path.offsets[tile];
    uint last = path.offsets[tile + 1];
    uint flat = local.y * 16u + local.x;
    int sum = path.backdrops[tile] * int(windingScale);
    for (uint chunk = first; chunk < last; chunk += 256u) {
        uint count = min(256u, last - chunk);
        if (flat < count) {
            shared[flat] = path.pieces[chunk + flat];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        for (uint p = 0; p < count; ++p) {
            sum += fixedContribution(shared[p], local.x, local.y);
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    uint2 localPixel = tileIndex.xy * 16u + local.xy;
    uint2 pixel = localPixel + uint2(u.origin.xy) * 16u;
    float coverage = windingToAlpha(float((sum + 128) >> 8) / 4096.0f, u.fillRule);
    if (u.clipId != 0u) {
        FrameBuffers frame;
        frame.transforms = transforms;
        frame.clips = clips;
        coverage *= clipCoverage(frame, ushort(u.clipId), float2(pixel) + 0.5f);
    }
    path.coverage[localPixel.y * u.columns * 16u + localPixel.x] = half(coverage);
}

struct PathCompositeOut {
    float4 position [[position]];
    uint path [[flat]];
};

vertex PathCompositeOut pathCompositeVertex(uint vertexId [[vertex_id]],
                                            uint instanceId [[instance_id]],
                                            constant BatchPath *paths [[buffer(0)]],
                                            constant FrameUniforms &frame [[buffer(4)]]) {
    constant PathUniforms &u = paths[instanceId].uniforms;
    float2 point = (float2(u.origin.xy) +
                    quadCorners[vertexId] * float2(u.columns, u.rows)) * 16.0f - frame.origin;
    PathCompositeOut out;
    out.position = float4(point.x / frame.viewport.x * 2.0f - 1.0f,
                           1.0f - point.y / frame.viewport.y * 2.0f, u.depth, 1.0f);
    out.path = instanceId;
    return out;
}

[[early_fragment_tests]] fragment float4 pathCompositeFragment(
    PathCompositeOut in [[stage_in]], constant BatchPath *paths [[buffer(0)]],
    constant FrameUniforms &frame [[buffer(4)]]) {
    constant BatchPath &path = paths[in.path];
    uint2 local = uint2(in.position.xy + frame.origin) - uint2(path.uniforms.origin.xy) * 16u;
    float coverage = float(path.coverage[local.y * path.uniforms.columns * 16u + local.x]);
    return outputColor(decodeColor(path.uniforms.color) * coverage, frame.outputColorSpace);
}
