kernel void shadowProbe(device const float4 *queries [[buffer(0)]], device float *results [[buffer(1)]],
                        constant uint &count [[buffer(2)]], uint index [[thread_position_in_grid]]) {
    if (index >= count) {
        return;
    }
    float4 point = queries[index * 2];
    float4 shape = queries[index * 2 + 1];
    results[index] = roundedBoxShadow(point.xy, point.zw, shape.x, shape.y);
}
