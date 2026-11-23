#include <metal_stdlib>
using namespace metal;

struct SpectrumBandUniforms {
    uint end;
    uint size;
    uint ringOffset;
    uint densityOffset;
    float scale;
    float binWidth;
    uint mask;
    uint log2Half;
};

struct SpectrumTerm {
    float weight;
    float low;
    float high;
    uint band;
};

constant float blackmanHarris0 = 0.35875f;
constant float blackmanHarris1 = 0.48829f;
constant float blackmanHarris2 = 0.14128f;
constant float blackmanHarris3 = 0.01168f;

float blackmanHarrisWindow(uint n, uint size) {
    float phase = 2.0f * M_PI_F * float(n) / float(size);
    return blackmanHarris0 - blackmanHarris1 * precise::cos(phase) +
           blackmanHarris2 * precise::cos(2.0f * phase) -
           blackmanHarris3 * precise::cos(3.0f * phase);
}

float2 complexMultiply(float2 a, float2 b) {
    return float2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

float2 twiddle(uint k, uint size) {
    float angle = -2.0f * M_PI_F * float(k) / float(size);
    return float2(precise::cos(angle), precise::sin(angle));
}

kernel void umaSpectrumBins(device const float *bands [[buffer(0)]],
                            constant SpectrumBandUniforms *uniforms [[buffer(1)]],
                            device float *density [[buffer(2)]],
                            uint band [[threadgroup_position_in_grid]],
                            uint lane [[thread_index_in_threadgroup]],
                            uint lanes [[threads_per_threadgroup]]) {
    threadgroup float2 data[2048];
    SpectrumBandUniforms u = uniforms[band];
    uint half_ = u.size / 2u;
    uint start = u.end - u.size;
    device const float *ring = bands + u.ringOffset;

    for (uint m = lane; m < half_; m += lanes) {
        uint n0 = 2u * m;
        uint n1 = n0 + 1u;
        float x0 = ring[(start + n0) & u.mask] * blackmanHarrisWindow(n0, u.size);
        float x1 = ring[(start + n1) & u.mask] * blackmanHarrisWindow(n1, u.size);
        uint reversed = reverse_bits(m) >> (32u - u.log2Half);
        data[reversed] = float2(x0, x1);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (uint span = 2u; span <= half_; span <<= 1u) {
        uint halfSpan = span >> 1u;
        for (uint butterfly = lane; butterfly < half_ / 2u; butterfly += lanes) {
            uint group = butterfly / halfSpan;
            uint offset = butterfly % halfSpan;
            uint top = group * span + offset;
            uint bottom = top + halfSpan;
            float2 w = twiddle(offset * (half_ / span), half_);
            float2 a = data[top];
            float2 b = complexMultiply(data[bottom], w);
            data[top] = a + b;
            data[bottom] = a - b;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    for (uint k = lane; k <= half_; k += lanes) {
        float2 z = data[k % half_];
        float2 mirror = data[(half_ - k) % half_];
        float2 conjugate = float2(mirror.x, -mirror.y);
        float2 even = 0.5f * (z + conjugate);
        float2 difference = 0.5f * (z - conjugate);
        float2 odd = float2(difference.y, -difference.x);
        float2 bin = even + complexMultiply(twiddle(k, u.size), odd);
        float edge = k == 0u || k == half_ ? 1.0f : 2.0f;
        density[u.densityOffset + k] = edge * dot(bin, bin) * u.scale;
    }
}

kernel void umaSpectrumKnots(device const float *density [[buffer(0)]],
                             constant SpectrumBandUniforms *uniforms [[buffer(1)]],
                             device const SpectrumTerm *terms [[buffer(2)]],
                             device float *power [[buffer(3)]], constant uint &knots [[buffer(4)]],
                             uint knot [[thread_position_in_grid]]) {
    if (knot >= knots) {
        return;
    }
    float sum = 0.0f;
    for (uint t = 0u; t < 4u; ++t) {
        SpectrumTerm term = terms[knot * 4u + t];
        if (term.weight <= 0.0f) {
            continue;
        }
        SpectrumBandUniforms u = uniforms[term.band];
        float width = u.binWidth;
        int last = int(u.size / 2u);
        int first = clamp(int(floor(term.low / width + 0.5f)), 0, last);
        int final_ = clamp(int(floor(term.high / width + 0.5f)), 0, last);
        float accumulated = 0.0f;
        for (int k = first; k <= final_; ++k) {
            float binLow = (float(k) - 0.5f) * width;
            float binHigh = (float(k) + 0.5f) * width;
            float overlap = min(term.high, binHigh) - max(term.low, binLow);
            if (overlap > 0.0f) {
                accumulated += overlap * density[u.densityOffset + uint(k)];
            }
        }
        sum += term.weight * accumulated / (term.high - term.low);
    }
    power[knot] = sum;
}
