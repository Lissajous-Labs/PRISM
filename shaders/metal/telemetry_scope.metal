#include <metal_stdlib>
using namespace metal;

struct TelemetryScopeUniforms {
    uint end;
    uint window;
    uint columns;
    uint mask;
    uint channel;
    float gain;
    uint reserved0;
    uint reserved1;
};

kernel void telemetryScope(device const float *left [[buffer(0)]],
                           device const float *right [[buffer(1)]],
                           constant TelemetryScopeUniforms &u [[buffer(2)]],
                           texture2d<float, access::write> strip [[texture(0)]],
                           uint column [[thread_position_in_grid]]) {
    if (column >= u.columns) {
        return;
    }
    uint first = uint((ulong(column) * u.window) / u.columns);
    uint last = max(first + 1, uint((ulong(column + 1) * u.window) / u.columns));
    uint start = u.end - u.window;
    float low = INFINITY;
    float high = -INFINITY;
    for (uint n = first; n < last; ++n) {
        uint at = (start + n) & u.mask;
        float l = left[at];
        float r = right[at];
        float value = u.channel == 0 ? l : (u.channel == 1 ? r : 0.5f * (l + r));
        value *= u.gain;
        low = min(low, value);
        high = max(high, value);
    }
    strip.write(float4(saturate((low + 1.0f) * 0.5f), saturate((high + 1.0f) * 0.5f), 0.0f, 0.0f),
                uint2(column, 0));
}
