#include <metal_stdlib>
using namespace metal;

struct PrimitiveDesc {
    float bounds[4];
    uint kindFlags;
    uint color;
    uint borderColor;
    uint depth;
    ushort transformId;
    ushort clipId;
    uint payloadOffset;
    float params[4];
    uint auxHandle;
    uint gradientOffset;
};

struct TransformRecord {
    float forward[6];
    float inverse[6];
};

struct ClipRecord {
    ushort parent;
    ushort transformId;
    uint kind;
    float bounds[4];
    float radii[4];
};

struct FrameUniforms {
    float2 viewport;
    uint maxDepth;
    uint textureCount;
    uint countFragments;
    uint gradientRows;
    uint reserved0;
    uint reserved1;
};

struct FrameBuffers {
    device const PrimitiveDesc *primitives;
    device const float *payload;
    device const TransformRecord *transforms;
    device const ClipRecord *clips;
    device const float4 *halftoneCells;
};

constant uint kindMask = 0xFFu;
constant uint borderFlag = 1u << 12;
constant uint gradientFlag = 1u << 13;
constant uint capShift = 14;
constant uint capMask = 3u << 14;
constant uint insetFlag = 1u << 16;
constant uint noClip = 0;
constant uint noTexture = 0xFFFFFFFFu;
constant float gradientStep = 1.0f / 64.0f;

constant uint kindRoundedRect = 0;
constant uint kindSquircle = 1;
constant uint kindArc = 2;
constant uint kindCapsule = 3;
constant uint kindBezier = 4;
constant uint kindTriangle = 5;
constant uint kindShadow = 6;
constant uint kindGlyphAlpha = 7;
constant uint kindGlyphLcd = 8;
constant uint kindGlyphMtsdf = 9;
constant uint kindTexture = 10;
constant uint kindPlot = 11;
constant uint kindHalftone = 12;
constant uint kindFormant = 14;

float2 payloadPoint(device const float *payload, uint offset) {
    return float2(payload[offset], payload[offset + 1]);
}

float2 applyInverse(TransformRecord t, float2 p) {
    return float2(t.inverse[0] * p.x + t.inverse[2] * p.y + t.inverse[4],
                  t.inverse[1] * p.x + t.inverse[3] * p.y + t.inverse[5]);
}

float2 applyForward(TransformRecord t, float2 p) {
    return float2(t.forward[0] * p.x + t.forward[2] * p.y + t.forward[4],
                  t.forward[1] * p.x + t.forward[3] * p.y + t.forward[5]);
}

float srgbDecode(float encoded) {
    float magnitude = abs(encoded);
    float linear = magnitude <= 0.04045f ? magnitude / 12.92f : pow((magnitude + 0.055f) / 1.055f, 2.4f);
    return copysign(linear, encoded);
}

float4 decodeColor(uint packed) {
    float alpha = float((packed >> 24) & 0xFFu) / 255.0f;
    return float4(srgbDecode(float(packed & 0xFFu) / 255.0f) * alpha,
                  srgbDecode(float((packed >> 8) & 0xFFu) / 255.0f) * alpha,
                  srgbDecode(float((packed >> 16) & 0xFFu) / 255.0f) * alpha, alpha);
}

float boxCoverage(float distance, float2 normal) {
    float ax = abs(normal.x);
    float ay = abs(normal.y);
    float norm = length(float2(ax, ay));
    float u = norm > 0.0f ? max(ax, ay) / norm : 1.0f;
    float v = norm > 0.0f ? min(ax, ay) / norm : 0.0f;
    float t = abs(distance);
    float lower = (u - v) * 0.5f;
    float upper = (u + v) * 0.5f;
    float outside = 0.0f;
    if (t <= lower) {
        outside = 0.5f - t / u;
    } else if (t < upper) {
        outside = (upper - t) * (upper - t) / (2.0f * u * v);
    }
    outside = clamp(outside, 0.0f, 1.0f);
    return distance >= 0.0f ? outside : 1.0f - outside;
}

float erfApprox(float x) {
    float magnitude = abs(x);
    float t = 1.0f / (1.0f + 0.3275911f * magnitude);
    float polynomial = ((((1.061405429f * t - 1.453152027f) * t + 1.421413741f) * t - 0.284496736f) * t + 0.254829592f) * t;
    return copysign(1.0f - polynomial * exp(-magnitude * magnitude), x);
}
