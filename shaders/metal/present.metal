float presentNoise(uint2 pixel, uint frame) {
    float offset = 5.588238f * float(frame % 64u);
    float x = float(pixel.x) + offset;
    float y = float(pixel.y) + offset;
    float inner = 0.06711056f * x + 0.00583715f * y;
    float scaled = 52.9829189f * (inner - floor(inner));
    return scaled - floor(scaled);
}

float srgbEncode(float linear) {
    float magnitude = abs(linear);
    float encoded = magnitude <= 0.0031308f ? 12.92f * magnitude : 1.055f * pow(magnitude, 1.0f / 2.4f) - 0.055f;
    return copysign(encoded, linear);
}

float triangularNoise(float uniformValue) {
    return uniformValue < 0.5f ? sqrt(2.0f * uniformValue) - 1.0f : 1.0f - sqrt(2.0f * (1.0f - uniformValue));
}

kernel void presentDither(texture2d<float, access::read> source [[texture(0)]], device uchar4 *output [[buffer(0)]],
                          constant uint &frame [[buffer(1)]], uint2 pixel [[thread_position_in_grid]]) {
    uint width = source.get_width();
    if (pixel.x >= width || pixel.y >= source.get_height()) {
        return;
    }
    float4 color = source.read(pixel);
    float noise = triangularNoise(presentNoise(pixel, frame));
    float3 levels = clamp(floor(float3(srgbEncode(color.r), srgbEncode(color.g), srgbEncode(color.b)) * 255.0f + 0.5f + noise), 0.0f, 255.0f);
    float alpha = clamp(floor(color.a * 255.0f + 0.5f), 0.0f, 255.0f);
    output[pixel.y * width + pixel.x] = uchar4(uchar(levels.r), uchar(levels.g), uchar(levels.b), uchar(alpha));
}
