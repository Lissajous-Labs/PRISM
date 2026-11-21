float formantHeightGpu(device const float *h, float dbRange, float x) {
    float u = (x - h[0]) / (h[2] - h[0]);
    float omega = exp(h[4] + u * h[5]) * h[6];
    float sineHalf = sin(omega * 0.5f), cosineHalf = cos(omega * 0.5f);
    float sine = 2.0f * sineHalf * cosineHalf, twiceSquare = 2.0f * sineHalf * sineHalf;
    uint count = as_type<uint>(h[12]);
    bool parallel = as_type<uint>(h[13]) != 0u;
    float2 total = parallel ? float2(0.0f) : float2(1.0f, 0.0f);
    for (uint i = 0; i < count; ++i) {
        device const float *s = h + 16u + i * 5u;
        float nr = s[0] - s[1] * twiceSquare, ni = s[2] * sine;
        float dr = s[3] - (2.0f - s[4]) * twiceSquare, di = s[4] * sine;
        float denominator = max(dr * dr + di * di, 1e-30f);
        float2 value = float2(nr * dr + ni * di, ni * dr - nr * di) / denominator;
        total = parallel ? total + value : float2(total.x * value.x - total.y * value.y, total.x * value.y + total.y * value.x);
    }
    float db = 10.0f * log10(max(dot(total, total), 1e-24f));
    float normalized = clamp((db - h[7]) / dbRange, 0.0f, 1.0f);
    return h[1] + (h[3] - h[1]) * (1.0f - normalized);
}
float2 formantSpanGpu(device const float *h, float dbRange, float x, float width, float height) {
    float samples[9];
    uint highest = 0u, lowest = 0u;
    for (uint j = 0; j <= 8u; ++j) {
        samples[j] = formantHeightGpu(h, dbRange, x + width * (float(j) / 8.0f - 0.5f));
        highest = samples[j] < samples[highest] ? j : highest;
        lowest = samples[j] > samples[lowest] ? j : lowest;
    }
    float2 span = float2(height);
    if (highest != 0u && highest != 8u) {
        float curvature = samples[highest - 1u] - 2.0f * samples[highest] + samples[highest + 1u];
        float difference = samples[highest - 1u] - samples[highest + 1u];
        span.x = min(height, curvature != 0.0f ? samples[highest] - difference * difference / (8.0f * curvature) : samples[highest]);
    }
    if (lowest != 0u && lowest != 8u) {
        float curvature = samples[lowest - 1u] - 2.0f * samples[lowest] + samples[lowest + 1u];
        float difference = samples[lowest - 1u] - samples[lowest + 1u];
        span.y = max(height, curvature != 0.0f ? samples[lowest] - difference * difference / (8.0f * curvature) : samples[lowest]);
    }
    return span;
}
float4 shadeFormantGpu(PrimitiveDesc d, device const float *payload, TransformRecord t, float2 local,
                       texture2d<float> columns, float2 physicalPixel) {
    device const float *h = payload + d.payloadOffset;
    float height, slope;
    float2 span;
    if (d.params[1] > 0.0f) {
        float4 column = columns.read(uint2(min(uint(physicalPixel.x), columns.get_width() - 1u), 0));
        height = column.x; slope = column.y; span = column.zw;
    } else {
        height = formantHeightGpu(h, d.params[0], local.x);
        slope = formantHeightGpu(h, d.params[0], local.x + 0.5f) - formantHeightGpu(h, d.params[0], local.x - 0.5f);
        span = formantSpanGpu(h, d.params[0], local.x, abs(t.inverse[0]), height);
    }
    float inverse = 1.0f / length(float2(slope, 1.0f));
    float2 normal = float2(-slope, 1.0f) * inverse;
    float2 physical = float2(t.inverse[0] * normal.x + t.inverse[1] * normal.y,
                             t.inverse[2] * normal.x + t.inverse[3] * normal.y);
    float perpendicular = abs(local.y - height) * inverse;
    float2 verticalNormal = float2(t.inverse[1], t.inverse[3]);
    float verticalNorm = length(verticalNormal);
    float spanDistance = max(span.x - local.y, local.y - span.y);
    float core = max(boxCoverage((perpendicular - h[8] * 0.5f) / length(physical), physical),
                     verticalNorm > 1e-12f ? boxCoverage((spanDistance - h[8] * 0.5f) / verticalNorm, verticalNormal) : 0.0f);
    float under = local.y > height
                      ? h[11] * (h[14] > 0.0f ? exp(-h[14] * (local.y - height) / (h[3] - h[1])) : 1.0f)
                      : 0.0f;
    float refLine = 0.0f;
    if (h[15] > 0.0f) {
        float zeroNorm = clamp(-h[7] / d.params[0], 0.0f, 1.0f);
        float zeroY = h[1] + (h[3] - h[1]) * (1.0f - zeroNorm);
        float2 lineNormal = float2(t.inverse[1], t.inverse[3]);
        float lineNorm = length(lineNormal);
        float lineCov = lineNorm > 1e-12f ? boxCoverage((abs(local.y - zeroY) - 0.5f) / lineNorm, lineNormal) : 0.0f;
        refLine = h[15] * lineCov;
    }
    float body = max(under, refLine);
    float coverage = core + (1.0f - core) * body;
    float halo = h[9] > 0.0f ? h[10] * exp(-perpendicular * perpendicular / (2.0f * h[9] * h[9])) : 0.0f;
    float4 color = decodeColor(d.color);
    return float4(color.rgb * (coverage + halo), color.a * coverage);
}

kernel void formantColumns(device const PrimitiveDesc *primitives [[buffer(0)]],
                            device const float *payload [[buffer(1)]],
                            device const TransformRecord *transforms [[buffer(2)]],
                            constant uint &index [[buffer(3)]],
                            texture2d<float, access::write> columns [[texture(0)]],
                            uint column [[thread_position_in_grid]]) {
    if (column >= columns.get_width()) { return; }
    PrimitiveDesc d = primitives[index];
    device const float *h = payload + d.payloadOffset;
    TransformRecord t = transforms[d.transformId];
    float x = t.inverse[0] * (float(column) + 0.5f) + t.inverse[4];
    if (x < h[0] || x > h[2]) { columns.write(float4(0.0f), uint2(column, 0)); return; }
    float height = formantHeightGpu(h, d.params[0], x);
    float slope = formantHeightGpu(h, d.params[0], x + 0.5f) - formantHeightGpu(h, d.params[0], x - 0.5f);
    columns.write(float4(height, slope, formantSpanGpu(h, d.params[0], x, abs(t.inverse[0]), height)), uint2(column, 0));
}
