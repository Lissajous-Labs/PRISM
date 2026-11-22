constant int fieldPrimitive = 0;
constant int fieldInnerBorder = 1;
constant int fieldInsetBox = 2;

struct ShadeOut {
    float4 color;
    float3 coverage;
    bool subpixel;
};

struct LocalBox {
    float2 center;
    float2 half_;
};

LocalBox payloadBox(device const float *payload, uint offset) {
    float2 low = payloadPoint(payload, offset);
    float2 high = payloadPoint(payload, offset + 2);
    LocalBox box;
    box.center = (low + high) * 0.5f;
    box.half_ = (high - low) * 0.5f;
    return box;
}

LocalBox rectBox(PrimitiveDesc d) {
    float margin = (d.kindFlags & kindMask) == kindShadow && (d.kindFlags & insetFlag) == 0u
                       ? 3 * d.params[0] + aaMargin
                       : aaMargin;
    float2 low = float2(d.bounds[0] + margin, d.bounds[1] + margin);
    float2 high = float2(d.bounds[2] - margin, d.bounds[3] - margin);
    LocalBox box;
    box.center = (low + high) * 0.5f;
    box.half_ = (high - low) * 0.5f;
    return box;
}

float4 primitiveRadii(PrimitiveDesc d) {
    return float4(d.params[0], d.params[1], d.params[2], d.params[3]);
}

float4 innerRadii(float4 radii, float4 widths) {
    return float4(max(0.0f, radii.x - max(widths.y, widths.z)), max(0.0f, radii.y - max(widths.w, widths.z)),
                  max(0.0f, radii.z - max(widths.y, widths.x)), max(0.0f, radii.w - max(widths.w, widths.x)));
}

struct RoundedShape {
    float2 center;
    float2 half_;
    float4 radii;
};

bool roundedShape(int mode, PrimitiveDesc d, device const float *payload, thread RoundedShape &shape) {
    if (mode == fieldPrimitive && (d.kindFlags & kindMask) != kindRoundedRect) {
        return false;
    }
    uint offset = d.payloadOffset;
    LocalBox box = rectBox(d);
    if (mode == fieldInnerBorder) {
        float4 widths = float4(payload[offset], payload[offset + 1], payload[offset + 2], payload[offset + 3]);
        shape.center = box.center + float2(widths.x - widths.z, widths.y - widths.w) * 0.5f;
        shape.half_ = max(box.half_ - float2(widths.x + widths.z, widths.y + widths.w) * 0.5f, float2(0.0f));
        shape.radii = innerRadii(primitiveRadii(d), widths);
        return true;
    }
    shape.center = box.center;
    shape.half_ = box.half_;
    if (mode == fieldInsetBox) {
        shape.radii = float4(d.params[1]);
        return true;
    }
    shape.radii = primitiveRadii(d);
    return true;
}

float2 plotBand(PrimitiveDesc d, device const float *payload, texture2d<float> strip, float2 p) {
    LocalBox box = payloadBox(payload, d.payloadOffset);
    float columns = d.params[1];
    float u = clamp((p.x - (box.center.x - box.half_.x)) / (2.0f * box.half_.x), 0.0f, 1.0f);
    int count = int(columns);
    int raw = int(floor(min(u * columns, columns - 1.0f) + d.params[2]));
    uint column = uint((raw % count + count) % count);
    float4 band = strip.read(uint2(min(column, strip.get_width() - 1), 0));
    float base = box.center.y + box.half_.y;
    bool fill = payload[d.payloadOffset + 4] > 0.0f;
    return float2(base - (fill ? band.x : band.y) * 2.0f * box.half_.y,
                  fill ? base : base - band.x * 2.0f * box.half_.y);
}

float plotField(PrimitiveDesc d, device const float *payload, texture2d<float> strip, float2 p) {
    float2 band = plotBand(d, payload, strip, p);
    return max(band.x - p.y, p.y - band.y) - d.params[0] * 0.5f;
}

float evaluateField(int mode, PrimitiveDesc d, device const float *payload, texture2d<float> strip, float2 p) {
    uint kind = d.kindFlags & kindMask;
    uint offset = d.payloadOffset;
    RoundedShape shape;
    if (roundedShape(mode, d, payload, shape)) {
        return sdRoundedRect(p - shape.center, shape.half_, shape.radii);
    }
    switch (kind) {
    case kindSquircle: {
        LocalBox box = rectBox(d);
        return sdSuperellipse(p - box.center, box.half_, d.params[0]);
    }
    case kindArc:
        return sdArc(p, payloadPoint(payload, offset), d.params[0], d.params[1], d.params[2], d.params[3],
                     (d.kindFlags & capMask) >> capShift);
    case kindCapsule:
        return sdSegment(p, payloadPoint(payload, offset), payloadPoint(payload, offset + 2)) - d.params[0];
    case kindBezier:
        return sdBezier(p, payloadPoint(payload, offset), payloadPoint(payload, offset + 2),
                        payloadPoint(payload, offset + 4)) - d.params[0];
    case kindTriangle:
        return sdRoundedTriangle(p, payloadPoint(payload, offset), payloadPoint(payload, offset + 2),
                                 payloadPoint(payload, offset + 4), d.params[0]);
    case kindPlot:
        if (!fullShading) {
            return -1.0f;
        }
        return plotField(d, payload, strip, p);
    default:
        return -1.0f;
    }
}

float fieldCoverage(int mode, TransformRecord t, PrimitiveDesc d, device const float *payload,
                    texture2d<float> strip, float2 local, float4 shape) {
    float reach = 0.71f * fast::sqrt(t.inverse[0] * t.inverse[0] + t.inverse[1] * t.inverse[1] +
                                     t.inverse[2] * t.inverse[2] + t.inverse[3] * t.inverse[3]);
    float distance;
    float2 gradient;
    RoundedShape rounded;
    if (roundedShape(mode, d, payload, rounded)) {
        float2 p = local - rounded.center;
        distance = sdRoundedRect(p, rounded.half_, rounded.radii);
        if (abs(distance) >= reach) {
            return distance < 0.0f ? 1.0f : 0.0f;
        }
        gradient = sdRoundedRectGradient(p, rounded.half_, rounded.radii);
    } else if (mode == fieldPrimitive && (d.kindFlags & kindMask) == kindArc) {
        ArcSample sample = sampleArc(local, payloadPoint(payload, d.payloadOffset), d.params[0],
                                     d.params[1], shape, (d.kindFlags & capMask) >> capShift);
        distance = sample.distance;
        if (abs(distance) >= reach) {
            return distance < 0.0f ? 1.0f : 0.0f;
        }
        gradient = sample.gradient;
    } else if (mode == fieldPrimitive && (d.kindFlags & kindMask) == kindCapsule) {
        FieldSample sample = sampleCapsule(local, payloadPoint(payload, d.payloadOffset),
                                           payloadPoint(payload, d.payloadOffset + 2), d.params[0]);
        distance = sample.distance;
        if (abs(distance) >= reach) {
            return distance < 0.0f ? 1.0f : 0.0f;
        }
        gradient = sample.gradient;
    } else if (fullShading && (d.kindFlags & kindMask) == kindPlot) {
        float2 band = plotBand(d, payload, strip, local);
        distance = max(band.x - local.y, local.y - band.y) - d.params[0] * 0.5f;
        if (abs(distance) >= reach) {
            return distance < 0.0f ? 1.0f : 0.0f;
        }
        gradient = float2(0.0f, band.x - local.y > local.y - band.y ? -1.0f : 1.0f);
    } else if (compactShading) {
        return 0.0f;
    } else {
        distance = evaluateField(mode, d, payload, strip, local);
        if (abs(distance) >= reach) {
            return distance < 0.0f ? 1.0f : 0.0f;
        }
        gradient = float2(
            evaluateField(mode, d, payload, strip, local + float2(gradientStep, 0.0f)) -
                evaluateField(mode, d, payload, strip, local - float2(gradientStep, 0.0f)),
            evaluateField(mode, d, payload, strip, local + float2(0.0f, gradientStep)) -
                evaluateField(mode, d, payload, strip, local - float2(0.0f, gradientStep))) / (2.0f * gradientStep);
    }
    float2 physical = float2(t.inverse[0] * gradient.x + t.inverse[1] * gradient.y,
                             t.inverse[2] * gradient.x + t.inverse[3] * gradient.y);
    float norm = fast::length(physical);
    if (!(norm > 1e-12f)) {
        return distance <= 0.0f ? 1.0f : 0.0f;
    }
    return boxCoverage(fast::divide(distance, norm), physical);
}

float radialParameter(float2 center, float2 focus, float radius, float2 p) {
    float2 axis = center - focus;
    float2 offset = p - focus;
    float a = dot(axis, axis) - radius * radius;
    float b = dot(offset, axis);
    float c = dot(offset, offset);
    float scale = max(max(radius * radius, dot(axis, axis)), 1e-12f);
    if (abs(a) <= 1e-6f * scale) {
        return b > 0.0f ? c / (2.0f * b) : 0.0f;
    }
    float root = sqrt(max(0.0f, b * b - a * c));
    float best = max((b + root) / a, (b - root) / a);
    return best >= 0.0f ? best : 0.0f;
}

float gradientParameter(uint shape, device const float *payload, uint offset, float2 p) {
    float2 first = payloadPoint(payload, offset);
    float2 second = payloadPoint(payload, offset + 2);
    if (shape == 1u) {
        return clamp(radialParameter(first, second, payload[offset + 4], p), 0.0f, 1.0f);
    }
    if (shape == 2u) {
        float turns = (atan2(p.y - first.y, p.x - first.x) - payload[offset + 2]) / 6.28318531f;
        return turns - floor(turns);
    }
    float2 axis = second - first;
    float lengthSquared = dot(axis, axis);
    return lengthSquared > 0.0f ? clamp(dot(p - first, axis) / lengthSquared, 0.0f, 1.0f) : 0.0f;
}

float4 fillColor(PrimitiveDesc d, device const float *payload, texture2d<float> gradients,
                 float2 local, float4 fill) {
    if ((d.kindFlags & gradientFlag) == 0u) {
        return fill;
    }
    float t = gradientParameter((d.kindFlags >> 17) & 3u, payload, d.gradientOffset, local);
    float position = t * 255.0f;
    uint lower = uint(floor(position));
    uint upper = min(lower + 1u, 255u);
    float weight = position - float(lower);
    float4 color = mix(gradients.read(uint2(lower, d.auxHandle)), gradients.read(uint2(upper, d.auxHandle)), weight);
    return color * fill.a;
}

float4 sampleBilinear(texture2d<float> texture, float2 uv) {
    float width = float(texture.get_width());
    float height = float(texture.get_height());
    float2 position = float2(uv.x * width - 0.5f, uv.y * height - 0.5f);
    float2 base = floor(position);
    float2 fraction = position - base;
    int2 maximumIndex = int2(int(width) - 1, int(height) - 1);
    int2 i0 = clamp(int2(base), int2(0), maximumIndex);
    int2 i1 = clamp(int2(base) + int2(1), int2(0), maximumIndex);
    float4 a = texture.read(uint2(i0.x, i0.y));
    float4 b = texture.read(uint2(i1.x, i0.y));
    float4 c = texture.read(uint2(i0.x, i1.y));
    float4 e = texture.read(uint2(i1.x, i1.y));
    return mix(mix(a, b, fraction.x), mix(c, e, fraction.x), fraction.y);
}

float clipCoverage(FrameBuffers frame, ushort clipId, float2 physical) {
    float coverage = 1.0f;
    for (int depth = 0; depth < 32 && clipId != noClip && coverage > 0.0f; ++depth) {
        ClipRecord clip = frame.clips[clipId];
        TransformRecord t = frame.transforms[clip.transformId];
        float2 local = applyInverse(t, physical);
        float2 center = float2(clip.bounds[0] + clip.bounds[2], clip.bounds[1] + clip.bounds[3]) * 0.5f;
        float2 half_ = float2(clip.bounds[2] - clip.bounds[0], clip.bounds[3] - clip.bounds[1]) * 0.5f;
        float4 radii = clip.kind == 2u ? float4(clip.radii[0], clip.radii[1], clip.radii[2], clip.radii[3]) : float4(0.0f);
        float values[5];
        float2 offsets[5] = {float2(0.0f), float2(gradientStep, 0.0f), float2(-gradientStep, 0.0f),
                             float2(0.0f, gradientStep), float2(0.0f, -gradientStep)};
        for (int i = 0; i < 5; ++i) {
            float2 q = local + offsets[i] - center;
            values[i] = clip.kind == 3u ? (length(q / half_) - 1.0f) * min(half_.x, half_.y)
                                        : sdRoundedRect(q, half_, radii);
        }
        float2 gradient = float2(values[1] - values[2], values[3] - values[4]) / (2.0f * gradientStep);
        float2 physicalGradient = float2(t.inverse[0] * gradient.x + t.inverse[1] * gradient.y,
                                         t.inverse[2] * gradient.x + t.inverse[3] * gradient.y);
        float norm = length(physicalGradient);
        coverage *= norm > 1e-12f ? boxCoverage(values[0] / norm, physicalGradient) : (values[0] <= 0.0f ? 1.0f : 0.0f);
        clipId = clip.parent;
    }
    return coverage;
}

float stemDarkening(float coverage, float luminance) {
    float beta = 0.28f * (1.0f - 2.0f * pow(clamp(luminance, 0.0f, 1.0f), 1.0f / 2.2f));
    return coverage * (1.0f + beta) / (1.0f + beta * coverage);
}

ShadeOut shadeTexture(PrimitiveDesc d, device const float *payload, TransformRecord t,
                      texture2d<float> texture, float2 local, float4 color) {
    ShadeOut result;
    result.coverage = float3(0.0f);
    result.subpixel = false;
    float u = (local.x - d.bounds[0]) / (d.bounds[2] - d.bounds[0]);
    float v = (local.y - d.bounds[1]) / (d.bounds[3] - d.bounds[1]);
    uint uvOffset = d.payloadOffset;
    float2 uv = float2(payload[uvOffset] + u * (payload[uvOffset + 2] - payload[uvOffset]),
                       payload[uvOffset + 1] + v * (payload[uvOffset + 3] - payload[uvOffset + 1]));
    float4 texel = sampleBilinear(texture, uv);
    uint kind = d.kindFlags & kindMask;
    float luminance = color.a > 0.0f ? dot(color.rgb, float3(0.2126f, 0.7152f, 0.0722f)) / color.a : 0.0f;
    if (kind == kindGlyphAlpha) {
        result.color = color * stemDarkening(texel.x, luminance);
    } else if (kind == kindGlyphLcd) {
        float3 k = float3(stemDarkening(texel.x, luminance), stemDarkening(texel.y, luminance),
                          stemDarkening(texel.z, luminance));
        result.color = float4(color.rgb * k, color.a * (k.x + k.y + k.z) / 3.0f);
        result.coverage = color.a * k;
        result.subpixel = true;
    } else if (kind == kindGlyphMtsdf) {
        float median = max(min(texel.x, texel.y), min(max(texel.x, texel.y), texel.z));
        float quadWidth = length(float2(t.forward[0], t.forward[1])) * (d.bounds[2] - d.bounds[0]);
        float texelsAcross = abs(payload[uvOffset + 2] - payload[uvOffset]) * float(texture.get_width());
        float screenRange = d.params[0] * quadWidth / max(texelsAcross, 1e-9f);
        result.color = color * boxCoverage((0.5f - median) * screenRange, float2(1.0f, 0.0f));
    } else {
        result.color = texel * color.a;
    }
    return result;
}

float4 shadeHalftoneGpu(PrimitiveDesc d, device const float *payload, device const float4 *cells,
                        TransformRecord t, float2 p);

ShadeOut shadePrimitive(FrameBuffers frame, PrimitiveDesc d, texture2d<float> gradients,
                        texture2d<float> texture, float2 physical, float4 fill, float4 border,
                        float4 shape) {
    ShadeOut result;
    result.color = float4(0.0f);
    result.coverage = float3(0.0f);
    result.subpixel = false;
    TransformRecord t = frame.transforms[d.transformId];
    float2 local = applyInverse(t, physical);
    if (local.x < d.bounds[0] || local.x > d.bounds[2] || local.y < d.bounds[1] || local.y > d.bounds[3]) {
        return result;
    }
    uint kind = d.kindFlags & kindMask;
    if (texturedShading && kind >= kindGlyphAlpha && kind <= kindTexture) {
        result = shadeTexture(d, frame.payload, t, texture, local, fill);
    } else if (fullShading && kind == kindHalftone) {
        result.color = shadeHalftoneGpu(d, frame.payload, frame.halftoneCells, t, local);
    } else if (fullShading && kind == kindFormant) {
        result.color = shadeFormantGpu(d, frame.payload, t, local, texture, physical);
    } else if (analyticShading && kind == kindShadow) {
        LocalBox box = rectBox(d);
        float blurred =
            compactShading || (d.auxHandle != noTexture && (d.kindFlags & insetFlag) == 0u)
                ? sampleShadowSprite(frame.shadowTable, frame.shadowSprites, d.auxHandle,
                                     local - box.center)
                : roundedBoxShadow(local - box.center, box.half_, d.params[1], d.params[0]);
        float alpha = blurred;
        if (!compactShading && (d.kindFlags & insetFlag) != 0u) {
            alpha = fieldCoverage(fieldInsetBox, t, d, frame.payload, texture, local, shape) *
                    (1.0f - blurred);
        }
        result.color = fill * alpha;
    } else if (analyticShading) {
        float outer = fieldCoverage(fieldPrimitive, t, d, frame.payload, texture, local, shape);
        float4 color = fillColor(d, frame.payload, gradients, local, fill);
        if (fullShading && kind == kindPlot && d.params[3] > 0.0f) {
            LocalBox box = payloadBox(frame.payload, d.payloadOffset);
            float u = (local.x - box.center.x + box.half_.x) / (2.0f * box.half_.x);
            color *= smoothstep(0.0f, d.params[3], u) * smoothstep(0.0f, d.params[3], 1.0f - u);
        }
        if (kind == kindRoundedRect && (d.kindFlags & borderFlag) != 0u) {
            float inner =
                fieldCoverage(fieldInnerBorder, t, d, frame.payload, texture, local, shape);
            result.color = color * (outer * inner) + border * (outer * (1.0f - inner));
        } else {
            result.color = color * outer;
        }
    }
    if (!compactShading && d.clipId != noClip) {
        float clip = clipCoverage(frame, d.clipId, physical);
        result.color *= clip;
        result.coverage *= clip;
    }
    return result;
}
