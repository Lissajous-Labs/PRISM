constexpr sampler linearClamp(coord::normalized, address::clamp_to_edge, filter::linear);
constant uint sharedBlurWidth = 256;
constant int maximumBlurRadius = 52;

struct EffectUniforms {
    int4 crop;
    float4 rect;
    float4 radii;
    float2 sourceSize;
    float2 targetSize;
    uint tapCount;
    uint horizontal;
    float intensity;
    float blend;
    float threshold;
    float knee;
    uint karis;
    uint reserved;
};

float4 sampleAt(texture2d<float> source, float2 pixel, float2 size) {
    return source.sample(linearClamp, pixel / size);
}

kernel void blurPairedPass(texture2d<float> source [[texture(0)]], texture2d<float, access::write> target [[texture(1)]],
                           device const float2 *taps [[buffer(0)]], constant EffectUniforms &u [[buffer(1)]],
                           uint2 pixel [[thread_position_in_grid]]) {
    if (pixel.x >= target.get_width() || pixel.y >= target.get_height()) {
        return;
    }
    float2 center = float2(pixel) + 0.5f;
    float2 axis = u.horizontal != 0u ? float2(1.0f, 0.0f) : float2(0.0f, 1.0f);
    float4 sum = taps[0].y * sampleAt(source, center, u.sourceSize);
    for (uint t = 1; t < u.tapCount; ++t) {
        sum += taps[t].y * (sampleAt(source, center + axis * taps[t].x, u.sourceSize) +
                            sampleAt(source, center - axis * taps[t].x, u.sourceSize));
    }
    target.write(sum, pixel);
}

kernel void blurSharedPass(texture2d<float, access::read> source [[texture(0)]], texture2d<float, access::write> target [[texture(1)]],
                           device const float *weights [[buffer(0)]], constant EffectUniforms &u [[buffer(1)]],
                           uint2 group [[threadgroup_position_in_grid]], uint local [[thread_index_in_threadgroup]]) {
    threadgroup float4 line[sharedBlurWidth + 2 * maximumBlurRadius];
    int radius = int(u.tapCount) - 1;
    bool horizontal = u.horizontal != 0u;
    int width = int(source.get_width());
    int height = int(source.get_height());
    int along = int(group.x * sharedBlurWidth);
    int across = int(group.y);
    for (int i = int(local); i < int(sharedBlurWidth) + 2 * radius; i += int(sharedBlurWidth)) {
        int position = along + i - radius;
        uint2 texel = horizontal ? uint2(clamp(position, 0, width - 1), uint(across)) : uint2(uint(across), clamp(position, 0, height - 1));
        line[i] = source.read(texel);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    int position = along + int(local);
    if ((horizontal && position >= width) || (!horizontal && position >= height)) {
        return;
    }
    float4 sum = weights[0] * line[int(local) + radius];
    for (int k = 1; k <= radius; ++k) {
        sum += weights[k] * (line[int(local) + radius + k] + line[int(local) + radius - k]);
    }
    target.write(sum, horizontal ? uint2(uint(position), uint(across)) : uint2(uint(across), uint(position)));
}

kernel void kawaseDownPass(texture2d<float> source [[texture(0)]], texture2d<float, access::write> target [[texture(1)]],
                           constant EffectUniforms &u [[buffer(1)]], uint2 pixel [[thread_position_in_grid]]) {
    if (pixel.x >= target.get_width() || pixel.y >= target.get_height()) {
        return;
    }
    float2 c = float2(pixel) * 2.0f + 1.0f;
    float4 sum = 0.5f * sampleAt(source, c, u.sourceSize);
    sum += 0.125f * (sampleAt(source, c + float2(-1, -1), u.sourceSize) + sampleAt(source, c + float2(1, -1), u.sourceSize) +
                     sampleAt(source, c + float2(-1, 1), u.sourceSize) + sampleAt(source, c + float2(1, 1), u.sourceSize));
    target.write(sum, pixel);
}

float4 cropBlock(texture2d<float> source, int2 corner, int2 low, int2 high) {
    return source.read(uint2(clamp(corner, low, high))) +
           source.read(uint2(clamp(corner + int2(1, 0), low, high))) +
           source.read(uint2(clamp(corner + int2(0, 1), low, high))) +
           source.read(uint2(clamp(corner + int2(1, 1), low, high)));
}

kernel void kawaseCropDownPass(texture2d<float> source [[texture(0)]],
                               texture2d<float, access::write> target [[texture(1)]],
                               constant EffectUniforms &u [[buffer(1)]],
                               uint2 pixel [[thread_position_in_grid]]) {
    uint2 size = uint2(target.get_width(), target.get_height());
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    int2 low = u.crop.xy;
    int2 base = low + int2(pixel) * 2;
    if (pixel.x != 0u && pixel.y != 0u && pixel.x + 1u < size.x && pixel.y + 1u < size.y) {
        float2 c = float2(base) + 1.0f;
        float4 sum = 0.5f * sampleAt(source, c, u.sourceSize);
        sum += 0.125f * (sampleAt(source, c + float2(-1, -1), u.sourceSize) +
                         sampleAt(source, c + float2(1, -1), u.sourceSize) +
                         sampleAt(source, c + float2(-1, 1), u.sourceSize) +
                         sampleAt(source, c + float2(1, 1), u.sourceSize));
        target.write(sum, pixel);
        return;
    }

    int2 high = u.crop.zw - 1;
    float4 center = cropBlock(source, base, low, high);
    float4 corners = cropBlock(source, base + int2(-1, -1), low, high) +
                     cropBlock(source, base + int2(1, -1), low, high) +
                     cropBlock(source, base + int2(-1, 1), low, high) +
                     cropBlock(source, base + int2(1, 1), low, high);
    target.write(0.125f * center + 0.03125f * corners, pixel);
}

float4 kawaseUpSample(texture2d<float> source, float2 c, float2 size) {
    float4 cardinal = sampleAt(source, c + float2(-1, 0), size) + sampleAt(source, c + float2(1, 0), size) +
                      sampleAt(source, c + float2(0, -1), size) + sampleAt(source, c + float2(0, 1), size);
    float4 diagonal = sampleAt(source, c + float2(-0.5f, -0.5f), size) + sampleAt(source, c + float2(0.5f, -0.5f), size) +
                      sampleAt(source, c + float2(-0.5f, 0.5f), size) + sampleAt(source, c + float2(0.5f, 0.5f), size);
    return cardinal / 12.0f + diagonal / 6.0f;
}

kernel void kawaseUpPass(texture2d<float> source [[texture(0)]], texture2d<float, access::write> target [[texture(1)]],
                         constant EffectUniforms &u [[buffer(1)]], uint2 pixel [[thread_position_in_grid]]) {
    if (pixel.x >= target.get_width() || pixel.y >= target.get_height()) {
        return;
    }
    float2 c = (float2(pixel) + 0.5f) * (u.sourceSize / u.targetSize);
    target.write(kawaseUpSample(source, c, u.sourceSize), pixel);
}

float effectMask(constant EffectUniforms &u, float2 p) {
    float2 center = (u.rect.xy + u.rect.zw) * 0.5f;
    float2 half_ = (u.rect.zw - u.rect.xy) * 0.5f;
    float d = sdRoundedRect(p - center, half_, u.radii);
    float2 gradient = sdRoundedRectGradient(p - center, half_, u.radii);
    float norm = length(gradient);
    return norm > 1e-12f ? boxCoverage(d / norm, gradient) : (d <= 0.0f ? 1.0f : 0.0f);
}

kernel void kawaseCompositePass(texture2d<float> lower [[texture(0)]],
                                texture2d<float> upper [[texture(1)]],
                                texture2d<float, access::read_write> target [[texture(2)]],
                                constant EffectUniforms &u [[buffer(1)]],
                                uint2 offset [[thread_position_in_grid]]) {
    uint2 pixel = uint2(u.crop.xy) + offset;
    if (int(pixel.x) >= u.crop.z || int(pixel.y) >= u.crop.w) {
        return;
    }

    float mask = effectMask(u, float2(pixel) + 0.5f);
    if (mask <= 0.0f) {
        return;
    }

    float2 c = (float2(offset) + 0.5f) * (u.sourceSize / u.targetSize);
    float4 blurred = kawaseUpSample(lower, c, u.sourceSize);
    if (u.blend > 0.0f) {
        blurred += (kawaseUpSample(upper, c, u.sourceSize) - blurred) * u.blend;
    }
    float4 current = target.read(pixel);
    target.write(current + (blurred - current) * mask, pixel);
}

kernel void effectCompositePass(texture2d<float, access::read> blurred [[texture(0)]],
                                texture2d<float, access::read_write> target [[texture(1)]], constant EffectUniforms &u [[buffer(1)]],
                                uint2 offset [[thread_position_in_grid]]) {
    uint2 pixel = uint2(u.crop.xy) + offset;
    if (int(pixel.x) >= u.crop.z || int(pixel.y) >= u.crop.w) {
        return;
    }

    float mask = effectMask(u, float2(pixel) + 0.5f);
    if (mask <= 0.0f) {
        return;
    }

    float4 current = target.read(pixel);
    target.write(current + (blurred.read(offset) - current) * mask, pixel);
}

float bloomLuma(float4 color) {
    return 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
}

kernel void bloomPrefilterPass(texture2d<float, access::read> source [[texture(0)]], texture2d<float, access::write> target [[texture(1)]],
                               constant EffectUniforms &u [[buffer(1)]], uint2 pixel [[thread_position_in_grid]]) {
    if (pixel.x >= target.get_width() || pixel.y >= target.get_height()) {
        return;
    }
    float4 sum = float4(0.0f);
    float weightSum = 0.0f;
    uint2 limit = uint2(source.get_width() - 1, source.get_height() - 1);
    for (uint j = 0; j < 2; ++j) {
        for (uint i = 0; i < 2; ++i) {
            float4 texel = source.read(min(pixel * 2u + uint2(i, j), limit));
            float luma = bloomLuma(texel);
            float soft = clamp(luma - u.threshold + u.knee, 0.0f, 2.0f * u.knee);
            float curve = soft * soft / (4.0f * u.knee + 1e-5f);
            float multiplier = max(curve, luma - u.threshold) / max(luma, 1e-5f);
            float weight = u.karis != 0u ? 1.0f / (1.0f + luma) : 1.0f;
            sum += texel * multiplier * weight;
            weightSum += weight;
        }
    }
    target.write(sum / weightSum, pixel);
}

kernel void bloomCompositePass(texture2d<float, access::read> scene [[texture(0)]], texture2d<float> glow [[texture(1)]],
                               texture2d<float, access::write> target [[texture(2)]], constant EffectUniforms &u [[buffer(1)]],
                               uint2 pixel [[thread_position_in_grid]]) {
    if (pixel.x >= target.get_width() || pixel.y >= target.get_height()) {
        return;
    }
    float2 c = (float2(pixel) + 0.5f) * (u.sourceSize / u.targetSize);
    float4 base = scene.read(pixel);
    float4 added = kawaseUpSample(glow, c, u.sourceSize);
    target.write(float4(base.rgb + u.intensity * added.rgb, base.a), pixel);
}
