constant float pathTile = 16.0f;
constant float pieceScale = 65535.0f / 16.0f;
constant float windingScale = 1048576.0f;
constant uint scanPartition = 256;
constant uint flagAggregate = 1u << 30;
constant uint flagPrefix = 2u << 30;
constant uint valueMask = (1u << 30) - 1u;

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

kernel void pathBinCount(device const float4 *segments [[buffer(0)]], constant PathUniforms &u [[buffer(1)]],
                         device atomic_uint *counts [[buffer(2)]], device atomic_int *rowDeltas [[buffer(3)]],
                         uint index [[thread_position_in_grid]]) {
    if (index >= u.segmentCount) {
        return;
    }
    float4 s = segments[index];
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

kernel void pathScan(device const uint *counts [[buffer(0)]], device uint *offsets [[buffer(1)]],
                     device atomic_uint *status [[buffer(2)]], device atomic_uint *ticket [[buffer(3)]],
                     constant PathUniforms &u [[buffer(4)]], uint local [[thread_position_in_threadgroup]]) {
    threadgroup uint partitionIndex;
    threadgroup uint values[scanPartition];
    threadgroup uint exclusivePrefix;
    if (local == 0) {
        partitionIndex = atomic_fetch_add_explicit(ticket, 1u, memory_order_relaxed);
        exclusivePrefix = 0;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    uint partition = partitionIndex;
    uint index = partition * scanPartition + local;
    values[local] = index < u.tileCount ? counts[index] : 0u;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint stride = 1; stride < scanPartition; stride <<= 1) {
        uint addend = local >= stride ? values[local - stride] : 0u;
        threadgroup_barrier(mem_flags::mem_threadgroup);
        values[local] += addend;
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    if (local == 0) {
        uint aggregate = values[scanPartition - 1];
        atomic_store_explicit(&status[partition], flagAggregate | aggregate, memory_order_relaxed);
        uint prefix = 0;
        int look = int(partition) - 1;
        while (look >= 0) {
            uint state = atomic_load_explicit(&status[look], memory_order_relaxed);
            if ((state & ~valueMask) == 0u) {
                continue;
            }
            prefix += state & valueMask;
            if ((state & ~valueMask) == flagPrefix) {
                break;
            }
            --look;
        }
        atomic_store_explicit(&status[partition], flagPrefix | (prefix + aggregate), memory_order_relaxed);
        exclusivePrefix = prefix;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (index < u.tileCount) {
        offsets[index + 1] = exclusivePrefix + values[local];
    }
    if (index == 0) {
        offsets[0] = 0;
    }
}

kernel void pathBackdrop(device const int *rowDeltas [[buffer(0)]], device int *backdrops [[buffer(1)]],
                         constant PathUniforms &u [[buffer(2)]], uint row [[thread_position_in_grid]]) {
    if (row >= u.rows) {
        return;
    }
    int running = 0;
    for (uint column = 0; column < u.columns; ++column) {
        running += rowDeltas[row * (u.columns + 1) + column];
        backdrops[row * u.columns + column] = running;
    }
}

kernel void pathScatter(device const float4 *segments [[buffer(0)]], constant PathUniforms &u [[buffer(1)]],
                        device const uint *offsets [[buffer(2)]], device atomic_uint *cursors [[buffer(3)]],
                        device uint2 *pieces [[buffer(4)]], uint index [[thread_position_in_grid]]) {
    if (index >= u.segmentCount) {
        return;
    }
    PieceSink sink;
    sink.cursors = cursors;
    sink.offsets = offsets;
    sink.pieces = pieces;
    sink.scatter = true;
    visitTilePieces(segments[index], u, sink);
    visitBoundarySteps(segments[index], u, sink);
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

kernel void pathFine(device const uint *offsets [[buffer(0)]], device const int *backdrops [[buffer(1)]],
                     device const uint2 *pieces [[buffer(2)]], constant PathUniforms &u [[buffer(3)]],
                     device const TransformRecord *transforms [[buffer(4)]], device const ClipRecord *clips [[buffer(5)]],
                     texture2d<float, access::read_write> target [[texture(0)]],
                     depth2d<float, access::read> depth [[texture(1)]], uint2 tileIndex [[threadgroup_position_in_grid]],
                     uint2 local [[thread_position_in_threadgroup]]) {
    threadgroup uint2 shared[256];
    uint tile = tileIndex.y * u.columns + tileIndex.x;
    uint first = offsets[tile];
    uint last = offsets[tile + 1];
    uint flat = local.y * 16u + local.x;
    int sum = backdrops[tile] * int(windingScale);
    for (uint chunk = first; chunk < last; chunk += 256u) {
        uint count = min(256u, last - chunk);
        if (flat < count) {
            shared[flat] = pieces[chunk + flat];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        for (uint p = 0; p < count; ++p) {
            sum += fixedContribution(shared[p], local.x, local.y);
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    uint2 pixel = tileIndex * 16u + local;
    if (pixel.x >= target.get_width() || pixel.y >= target.get_height()) {
        return;
    }
    if (int(pixel.x) < u.damage.x || int(pixel.y) < u.damage.y || int(pixel.x) >= u.damage.z || int(pixel.y) >= u.damage.w) {
        return;
    }
    if (depth.read(pixel) < u.depth) {
        return;
    }
    float coverage = windingToAlpha(float((sum + 128) >> 8) / 4096.0f, u.fillRule);
    if (coverage <= 0.0f) {
        return;
    }
    if (u.clipId != 0u) {
        FrameBuffers frame;
        frame.transforms = transforms;
        frame.clips = clips;
        coverage *= clipCoverage(frame, ushort(u.clipId), float2(pixel) + 0.5f);
    }
    float4 source = decodeColor(u.color) * coverage;
    float4 destination = target.read(pixel);
    target.write(source + (1.0f - source.a) * destination, pixel);
}
