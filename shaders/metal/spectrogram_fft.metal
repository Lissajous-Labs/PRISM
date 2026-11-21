struct SpectrogramUniforms {
    uint frames, period, rows, first;
    float tilt, mirror, bend, sync;
    float phaseOffset, quantization, sharpness, amAmount;
    float amHarmonic, amPhase;
    uint bits, reserved;
};
float spectralCubic(float a, float b, float c, float d, float t) {
    return b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
}
float spectralFrame(texture2d<float, access::read> table, uint frame, float phase, uint period) {
    float turns = phase / (2.0f * M_PI_F);
    float position = (turns - floor(turns)) * float(period);
    int base = int(floor(position));
    uint mask = period - 1u;
    return spectralCubic(table.read(uint2(uint(base - 1) & mask, frame)).r,
                          table.read(uint2(uint(base) & mask, frame)).r,
                          table.read(uint2(uint(base + 1) & mask, frame)).r,
                          table.read(uint2(uint(base + 2) & mask, frame)).r, position - floor(position));
}
float spectralWarp(texture2d<float, access::read> table, uint frame, uint n, constant SpectrogramUniforms &u) {
    float theta = 2.0f * M_PI_F * float(n) / float(u.period);
    float turns = (theta + u.phaseOffset) / (2.0f * M_PI_F);
    turns -= floor(turns);
    if (u.mirror > 0.0f) {
        float peak = 1.0f - 0.5f * clamp(u.mirror, 0.0f, 1.0f);
        turns = turns <= peak ? turns / peak : 1.0f - (turns - peak) / (1.0f - peak);
    }
    if (u.bend != 0.0f) { turns = pow(turns, exp2(u.bend)); }
    turns *= u.sync;
    float value = spectralFrame(table, frame, turns * 2.0f * M_PI_F, u.period);
    if (u.quantization > 0.0f) {
        float q = value * u.quantization, f = q - floor(q);
        value = (floor(q) + 0.5f + 0.5f * tanh(u.sharpness * (f - 0.5f)) / tanh(u.sharpness * 0.5f)) / u.quantization;
    }
    return value * (1.0f - u.amAmount + u.amAmount * cos(u.amHarmonic * theta + u.amPhase));
}
kernel void spectrogramFft(texture2d<float, access::read> table [[texture(0)]],
                            texture2d<float, access::write> field [[texture(1)]],
                            constant SpectrogramUniforms &u [[buffer(0)]],
                            uint tid [[thread_index_in_threadgroup]], uint group [[threadgroup_position_in_grid]],
                            uint threads [[threads_per_threadgroup]]) {
    threadgroup float2 work[2048];
    threadgroup float2 twiddle[1024];
    uint frame = u.first + group;
    uint half_ = u.period / 2u;
    for (uint i = tid; i < half_; i += threads) {
        float cosine;
        float sine = precise::sincos(-2.0f * M_PI_F * float(i) / float(u.period), cosine);
        twiddle[i] = float2(cosine, sine);
    }
    for (uint i = tid; i < u.period; i += threads) {
        uint reversed = u.bits == 0u ? 0u : reverse_bits(i) >> (32u - u.bits);
        work[reversed] = float2(spectralWarp(table, frame, i, u), 0.0f);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint level = 0; level < u.bits; ++level) {
        uint halfSize = 1u << level;
        uint stride = u.bits - 1u - level;
        for (uint i = tid; i < half_; i += threads) {
            uint k = i & (halfSize - 1u), start = (i >> level) << (level + 1u);
            float2 w = twiddle[k << stride];
            float2 a = work[start + k], b = work[start + k + halfSize];
            float2 t = float2(w.x * b.x - w.y * b.y, w.x * b.y + w.y * b.x);
            work[start + k] = a + t;
            work[start + k + halfSize] = a - t;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float maximum = min(512.0f, float(u.period) * 0.5f - 1.0f);
    for (uint j = tid; j < u.rows; j += threads) {
        float k0 = j == 0u ? 0.5f : pow(maximum, float(j) / float(u.rows));
        float k1 = j + 1u == u.rows ? maximum + 0.5f : pow(maximum, float(j + 1u) / float(u.rows));
        uint first = uint(max(1.0f, ceil(k0 - 0.5f))), last = uint(min(maximum, floor(k1 + 0.5f)));
        float power = 0.0f;
        for (uint k = first; k <= last; ++k) {
            float overlap = max(0.0f, min(k1, float(k) + 0.5f) - max(k0, float(k) - 0.5f));
            power += overlap * dot(work[k], work[k]) * 4.0f / float(u.period * u.period) * pow(float(k), 2.0f * u.tilt);
        }
        power /= k1 - k0 + 1.0f;
        float energy = clamp((10.0f * log10(power + 2.51188643150958e-7f) + 66.0f) / 66.0f, 0.0f, 1.0f);
        field.write(float4(energy), uint2(frame, u.rows - 1u - j));
    }
}
void spectrogramScan(threadgroup float *values, uint local, uint threads) {
    for (uint stride = 1; stride < threads; stride <<= 1u) {
        float addend = local >= stride ? values[local - stride] : 0.0f;
        threadgroup_barrier(mem_flags::mem_threadgroup);
        values[local] += addend;
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
}
kernel void spectrogramPrefixRows(texture2d<float, access::read> field [[texture(0)]],
                                   texture2d<float, access::write> prefix [[texture(1)]],
                                   uint row [[threadgroup_position_in_grid]],
                                   uint local [[thread_index_in_threadgroup]],
                                   uint threads [[threads_per_threadgroup]]) {
    threadgroup float values[1024];
    uint width = field.get_width();
    float carry = 0.0f;
    if (local == 0u) { prefix.write(float4(0.0f), uint2(0, row + 1u)); }
    for (uint base = 0; base < width; base += threads) {
        uint x = base + local;
        values[local] = x < width ? field.read(uint2(x, row)).r : 0.0f;
        threadgroup_barrier(mem_flags::mem_threadgroup);
        spectrogramScan(values, local, threads);
        if (x < width) { prefix.write(float4(carry + values[local], 0.0f, 0.0f, 1.0f), uint2(x + 1u, row + 1u)); }
        carry += values[threads - 1u];
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
}
kernel void spectrogramPrefixColumns(texture2d<float, access::read_write> prefix [[texture(0)]],
                                      uint column [[threadgroup_position_in_grid]],
                                      uint local [[thread_index_in_threadgroup]],
                                      uint threads [[threads_per_threadgroup]]) {
    threadgroup float values[1024];
    uint height = prefix.get_height();
    float carry = 0.0f;
    if (local == 0u) { prefix.write(float4(0.0f), uint2(column, 0)); }
    for (uint base = 1; base < height; base += threads) {
        uint y = base + local;
        values[local] = y < height ? prefix.read(uint2(column, y)).r : 0.0f;
        threadgroup_barrier(mem_flags::mem_threadgroup);
        spectrogramScan(values, local, threads);
        if (y < height) { prefix.write(float4(carry + values[local], 0.0f, 0.0f, 1.0f), uint2(column, y)); }
        carry += values[threads - 1u];
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
}
