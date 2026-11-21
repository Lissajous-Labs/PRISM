float halftonePrefix(texture2d<float> texture, float2 uv) {
    float2 dimensions = float2(texture.get_width(), texture.get_height());
    return sampleBilinear(texture, (clamp(uv, 0.0f, 1.0f) * (dimensions - 1.0f) + 0.5f) / dimensions).r;
}

float halftoneEnergy(texture2d<float> texture, device const float *h, float2 center, float2 pitch, uint mode) {
    float2 size = float2(h[2] - h[0], h[3] - h[1]);
    float2 uv = (center - float2(h[0], h[1])) / size;
    if ((mode & 16u) == 0u) {
        return sampleBilinear(texture, uv).r;
    }
    float2 low = clamp(uv - pitch / (2.0f * size), 0.0f, 1.0f);
    float2 high = clamp(uv + pitch / (2.0f * size), 0.0f, 1.0f);
    float area = (high.x - low.x) * (high.y - low.y) * float(texture.get_width() - 1) * float(texture.get_height() - 1);
    float total = halftonePrefix(texture, high) - halftonePrefix(texture, float2(low.x, high.y)) -
                  halftonePrefix(texture, float2(high.x, low.y)) + halftonePrefix(texture, low);
    return area > 0.0f ? clamp(total / area, 0.0f, 1.0f) : 0.0f;
}

float halftoneShape(float2 p, float2 center, float2 axis, float2 halfSize, float fill, float radius, float power, uint mode) {
    float2 delta = p - center;
    float2 q = float2(dot(delta, axis), dot(delta, float2(-axis.y, axis.x)));
    float2 extent = float2(halfSize.x * fill, halfSize.y);
    if ((mode & 4u) != 0u) {
        return sdSuperellipse(q, extent, power);
    }
    return sdRoundedRect(q, extent, float4(min(radius, min(extent.x, extent.y))));
}

float halftoneCoverage(TransformRecord t, float2 p, float2 center, float2 axis, float2 halfSize,
                       float fill, float radius, float power, uint mode) {
    float d = halftoneShape(p, center, axis, halfSize, fill, radius, power, mode);
    float reach = 0.71f * sqrt(t.inverse[0] * t.inverse[0] + t.inverse[1] * t.inverse[1] +
                              t.inverse[2] * t.inverse[2] + t.inverse[3] * t.inverse[3]);
    if (abs(d) >= reach) {
        return d < 0.0f ? 1.0f : 0.0f;
    }
    if ((mode & 4u) == 0u || power == 2.0f) {
        CoveragePolygon pixel;
        pixel.count = 4;
        float2 corners[4] = {float2(-0.5f, -0.5f), float2(0.5f, -0.5f),
                              float2(0.5f, 0.5f), float2(-0.5f, 0.5f)};
        for (uint i = 0; i < 4; ++i) {
            float2 q = p - center + float2(t.inverse[0] * corners[i].x + t.inverse[2] * corners[i].y,
                                            t.inverse[1] * corners[i].x + t.inverse[3] * corners[i].y);
            pixel.points[i] = float2(dot(q, axis), dot(q, float2(-axis.y, axis.x)));
        }
        float value;
        if ((mode & 4u) != 0u) {
            for (uint i = 0; i < 4; ++i) { pixel.points[i] /= float2(halfSize.x * fill, halfSize.y); }
            value = coverageDisk(pixel, float2(0.0f), 1.0f);
        } else {
            value = roundedSpotPixelArea(pixel, float2(halfSize.x * fill, halfSize.y), radius);
        }
        return clamp(value / coveragePolygonArea(pixel), 0.0f, 1.0f);
    }
    float2 dx = float2(gradientStep, 0.0f), dy = float2(0.0f, gradientStep);
    float2 gradient = float2(halftoneShape(p + dx, center, axis, halfSize, fill, radius, power, mode) -
                                 halftoneShape(p - dx, center, axis, halfSize, fill, radius, power, mode),
                             halftoneShape(p + dy, center, axis, halfSize, fill, radius, power, mode) -
                                 halftoneShape(p - dy, center, axis, halfSize, fill, radius, power, mode)) / (2.0f * gradientStep);
    float2 physical = float2(t.inverse[0] * gradient.x + t.inverse[1] * gradient.y,
                             t.inverse[2] * gradient.x + t.inverse[3] * gradient.y);
    float norm = length(physical);
    return norm > 1e-12f ? boxCoverage(d / norm, physical) : (d <= 0.0f ? 1.0f : 0.0f);
}

float halftoneIgnition(float e, float threshold, float mixAmount, float spread, float width) {
    e = clamp(e, 0.0f, 1.0f);
    return e * mix(1.0f, smoothstep(spread * threshold, spread * threshold + width, e), mixAmount);
}

float halftoneArea(float2 halfSize, float fill, float radius, float area, float power, uint mode) {
    if ((mode & 4u) != 0u) {
        return 4.0f * halfSize.x * fill * halfSize.y * power / area;
    }
    float r = min(radius, min(halfSize.x * fill, halfSize.y));
    return (4.0f * halfSize.x * fill * halfSize.y - (4.0f - M_PI_F) * r * r) / area;
}

float3 shadeHalftoneCellGpu(PrimitiveDesc d, device const float *payload, TransformRecord t,
                            texture2d<float> texture, texture2d<float> blueNoise, texture2d<float> gradients,
                            float2 p, float2 cell, float2 center, float eta, bool primary) {
    device const float *h = payload + d.payloadOffset;
    uint mode = as_type<uint>(h[28]);
    float2 b1 = float2(h[4], h[5]), b2 = float2(h[6], h[7]), origin = float2(h[12], h[13]);
    float area = abs(b1.x * b2.y - b1.y * b2.x);
    float2 pitch = float2(length(b1), area / length(b1));
    float2 axis = b1 / pitch.x;
    float2 halfSize = max(float2(0.0f), (pitch - float2(h[14], h[15])) * 0.5f);
    float reach = 0.71f * sqrt(t.inverse[0] * t.inverse[0] + t.inverse[1] * t.inverse[1] +
                               t.inverse[2] * t.inverse[2] + t.inverse[3] * t.inverse[3]);
    float2 delta = p - center;
    if (!primary && (eta == 0.0f || abs(dot(delta, axis)) > halfSize.x + reach ||
                      abs(dot(delta, float2(-axis.y, axis.x))) > halfSize.y + reach)) {
        return float3(0.0f);
    }
    float energy = 0.0f, ridge = 0.0f;
    if ((mode & 1u) != 0u) {
        float u = (center.x - h[0]) / (h[2] - h[0]);
        float4 band = sampleBilinear(texture, float2(u, 0.5f));
        float rows = (h[3] - h[1]) / pitch.y;
        float j = floor((h[3] - center.y) / pitch.y), rh = band.x * rows;
        float fill = clamp(rh - j, 0.0f, 1.0f);
        float xi = clamp((j + 0.5f) / max(rh, 1.0f), 0.0f, 1.0f);
        float body = fill * (0.14f + 0.86f * pow(xi, 1.65f)) * (0.32f + 0.68f * pow(max(0.0f, band.x), 0.55f));
        float cap = 0.48f * smoothstep(0.0f, 0.02f, band.x) * exp(-pow(j + 1.0f - rh, 2.0f) / (2.0f * 0.55f * 0.55f));
        float peak = 0.96f * smoothstep(0.0f, 0.04f, band.y) * exp(-pow(j + 1.0f - band.y * rows, 2.0f) / (2.0f * 0.36f * 0.36f));
        float node = fill * 0.55f * band.z * exp(-pow(j + 0.5f - 0.62f * rh, 2.0f) / 0.72f);
        energy = clamp(max(body + cap + node, peak), 0.0f, 1.0f);
        ridge = energy;
    } else {
        energy = halftoneEnergy(texture, h, center, pitch, mode);
        ridge = 0.25f * halftoneEnergy(texture, h, center - b1, pitch, mode) + 0.5f * energy +
                0.25f * halftoneEnergy(texture, h, center + b1, pitch, mode);
        if ((mode & 8u) != 0u) {
            float u = (center.x - h[0]) / (h[2] - h[0]);
            float delta = (u - h[24]) / h[26];
            energy = min(1.0f, energy * (1.0f + h[25] * exp(-0.5f * delta * delta)));
        }
    }
    float threshold = 0.0f;
    if (as_type<uint>(h[30]) != noTexture) {
        threshold = blueNoise.read(uint2(int2(cell)) & uint2(63u)).r * (255.0f / 256.0f);
    }
    float tone = halftoneIgnition(energy, threshold, h[20], h[21], h[22]);
    float widthTone = halftoneIgnition(ridge, threshold, h[20], h[21], h[22]);
    float fillX = h[18] + (1.0f - h[18]) * pow(widthTone, h[19]);
    float spot = eta > 0.0f ? eta * halftoneCoverage(t, p, center, axis, halfSize, fillX, h[16], h[17], mode) : 0.0f;
    float grid = eta > 0.0f ? eta * halftoneCoverage(t, p, center, axis, halfSize, 1.0f, h[16], h[17], mode) : 0.0f;
    if (primary) {
        spot += (1.0f - eta) * halftoneArea(halfSize, fillX, h[16], area, d.params[0], mode);
        grid += (1.0f - eta) * halftoneArea(halfSize, 1.0f, h[16], area, d.params[0], mode);
    }
    float position = tone * 255.0f;
    uint lower = uint(position), upper = min(lower + 1u, 255u);
    float3 emit = mix(gradients.read(uint2(lower, d.gradientOffset)).rgb, gradients.read(uint2(upper, d.gradientOffset)).rgb, position - float(lower));
    float3 background = float3(0.0033465358f, 0.0036765073f, 0.0043914420f), gridColor = float3(0.0095f, 0.0072f, 0.0060f);
    return grid * (gridColor - background) + spot * (emit - gridColor);
}

float4 shadeHalftoneGpu(PrimitiveDesc d, device const float *payload, TransformRecord t,
                        texture2d<float> texture, texture2d<float> blueNoise, texture2d<float> gradients, float2 p) {
    device const float *h = payload + d.payloadOffset;
    uint mode = as_type<uint>(h[28]);
    float2 b1 = float2(h[4], h[5]), b2 = float2(h[6], h[7]), origin = float2(h[12], h[13]);
    float2 relative = p - origin;
    float det = b1.x * b2.y - b1.y * b2.x;
    float2 coordinates = float2(b2.y * relative.x - b2.x * relative.y, b1.x * relative.y - b1.y * relative.x) / det;
    float2 cell = floor(coordinates);
    float shift = 0.5f;
    if ((mode & 2u) != 0u) {
        float3 cube = float3(coordinates, -coordinates.x - coordinates.y);
        float3 rounded = round(cube), error = abs(rounded - cube);
        if (error.x > error.y + 1e-5f && error.x > error.z + 1e-5f) {
            rounded.x = -rounded.y - rounded.z;
        } else if (error.y > error.z + 1e-5f) {
            rounded.y = -rounded.x - rounded.z;
        }
        cell = rounded.xy;
        shift = 0.0f;
    }
    float2 center = origin + b1 * (cell.x + shift) + b2 * (cell.y + shift);
    float2 physicalB1 = float2(t.forward[0] * b1.x + t.forward[2] * b1.y, t.forward[1] * b1.x + t.forward[3] * b1.y);
    float2 physicalB2 = float2(t.forward[0] * b2.x + t.forward[2] * b2.y, t.forward[1] * b2.x + t.forward[3] * b2.y);
    float eta = smoothstep(3.6f, 7.2f, min(length(physicalB1), length(physicalB2)));
    float3 color = float3(0.0033465358f, 0.0036765073f, 0.0043914420f);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            color += shadeHalftoneCellGpu(d, payload, t, texture, blueNoise, gradients, p,
                                          cell + float2(x, y), center + float(x) * b1 + float(y) * b2,
                                          eta, x == 0 && y == 0);
        }
    }
    if ((mode & 8u) != 0u) {
        float sx = length(float2(t.forward[0], t.forward[1]));
        float x = h[0] + h[24] * (h[2] - h[0]);
        float snapped = (floor(x * sx) + 0.5f) / sx;
        float cursor = boxCoverage(abs(p.x - snapped) * sx - 0.75f, float2(1.0f, 0.0f));
        float2 a = float2(snapped - 4.0f / sx, h[1]), b = float2(snapped + 4.0f / sx, h[1]), c = float2(snapped, h[1] + 5.0f / sx);
        float capD = sdRoundedTriangle(p, a, b, c, 0.4f / sx);
        float2 dx = float2(gradientStep, 0.0f), dy = float2(0.0f, gradientStep);
        float2 grad = float2(sdRoundedTriangle(p + dx, a, b, c, 0.4f / sx) - sdRoundedTriangle(p - dx, a, b, c, 0.4f / sx),
                             sdRoundedTriangle(p + dy, a, b, c, 0.4f / sx) - sdRoundedTriangle(p - dy, a, b, c, 0.4f / sx)) / (2.0f * gradientStep);
        float2 physical = float2(t.inverse[0] * grad.x + t.inverse[1] * grad.y, t.inverse[2] * grad.x + t.inverse[3] * grad.y);
        float norm = length(physical);
        float cap = norm > 1e-12f ? boxCoverage(capD / norm, physical) : (capD <= 0.0f ? 1.0f : 0.0f);
        cursor = max(cursor, cap);
        int voices = clamp(int(h[27]), 1, 16);
        for (int v = 0; v < voices && voices > 1; ++v) {
            float voiceX = snapped + (float(v) / float(voices - 1) - 0.5f) * 0.05f * (h[2] - h[0]);
            cursor = max(cursor, 0.18f * boxCoverage(abs(p.x - voiceX) * sx - 0.35f, float2(1.0f, 0.0f)));
        }
        color = mix(color, float3(0.686685f, 0.715694f, 0.760525f), cursor);
    }
    return float4(color, 1.0f);
}
