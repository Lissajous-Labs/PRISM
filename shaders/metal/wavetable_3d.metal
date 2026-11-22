struct WavetableVertex {
    float4 position [[position]];
    float4 data;
    float2 normal;
    uint primitive [[flat]];
    uint mode [[flat]];
    uint flags [[flat]];
};
float waveExtra(device const float *h, uint extra, uint voices) {
    if (extra == 0u) { return h[12]; }
    float offset = voices > 1u ? 2.0f * float(extra - 1u) / float(voices - 1u) - 1.0f : 1.0f;
    return clamp(h[12] + h[14] * offset, 0.0f, 1.0f);
}
float waveSlice(device const float *h, uint index, thread bool &active, thread int &voice) {
    uint slices = as_type<uint>(h[16]), voices = as_type<uint>(h[18]);
    bool descending = (as_type<uint>(h[19]) & 1u) != 0u;
    uint ranks[5];
    for (uint e = 0; e <= voices; ++e) {
        float p = waveExtra(h, e, voices), distance = descending ? 1.0f - p : p;
        uint rank = uint(ceil(distance * float(slices - 1u)));
        for (uint j = 0; j <= voices; ++j) {
            float q = waveExtra(h, j, voices);
            rank += (descending ? q > p : q < p) || (q == p && j < e);
        }
        ranks[e] = rank;
        if (rank == index) { active = e == 0u; voice = int(e) - 1; return p; }
    }
    uint structural = index;
    for (uint e = 0; e <= voices; ++e) { structural -= ranks[e] < index; }
    active = false; voice = -1;
    float position = float(structural) / float(slices - 1u);
    return descending ? 1.0f - position : position;
}
float waveFrame(texture2d<float> source, int frame, float phase) {
    float turns = phase / (2.0f * M_PI_F);
    float position = (turns - floor(turns)) * float(source.get_width());
    int base = int(floor(position)), size = int(source.get_width());
    uint row = uint(clamp(frame, 0, int(source.get_height()) - 1));
    uint i0 = uint((base - 1 + size) % size), i1 = uint(base % size);
    uint i2 = uint((base + 1) % size), i3 = uint((base + 2) % size);
    return spectralCubic(source.read(uint2(i0, row)).r, source.read(uint2(i1, row)).r,
                          source.read(uint2(i2, row)).r, source.read(uint2(i3, row)).r, position - floor(position));
}
float waveAmplitude(device const float *h, texture2d<float> source, float theta, float morph, int voice) {
    float offset = h[23];
    uint voices = as_type<uint>(h[18]);
    if (voice >= 0) {
        float spread = voices > 1u ? 2.0f * float(voice) / float(voices - 1u) - 1.0f : 1.0f;
        offset += spread * (h[29] * h[30] + h[31]);
    }
    float turns = (theta + offset) / (2.0f * M_PI_F);
    turns -= floor(turns);
    if (h[20] > 0.0f) {
        float peak = 1.0f - 0.5f * clamp(h[20], 0.0f, 1.0f);
        turns = turns <= peak ? turns / peak : 1.0f - (turns - peak) / (1.0f - peak);
    }
    if (h[21] != 0.0f) { turns = pow(turns, exp2(h[21])); }
    turns *= h[22];
    float phase = turns * (2.0f * M_PI_F);
    float frame = clamp(morph, 0.0f, 1.0f) * float(source.get_height() - 1u);
    int base = int(floor(frame));
    float value = spectralCubic(waveFrame(source, base - 1, phase), waveFrame(source, base, phase),
                                 waveFrame(source, base + 1, phase), waveFrame(source, base + 2, phase), frame - floor(frame));
    if (h[24] > 0.0f) {
        float q = value * h[24], f = q - floor(q);
        value = (floor(q) + 0.5f + 0.5f * tanh(h[25] * (f - 0.5f)) / tanh(h[25] * 0.5f)) / h[24];
    }
    value *= 1.0f - h[26] + h[26] * cos(h[27] * theta + h[28]);
    return clamp(value, -1.0f, 1.0f);
}
float2 waveProject(device const float *h, float3 q, thread float &depth) {
    uint depthFixed = as_type<uint>(h[19]) >> 16;
    float dzScale = depthFixed != 0u ? float(depthFixed) / 1024.0f : 1.0f;
    float sz = q.z * dzScale;
    float xr = h[4] * q.x + h[5] * sz, zr = -h[5] * q.x + h[4] * sz;
    float yr = h[6] * h[9] * q.y - h[7] * zr;
    depth = h[7] * h[9] * q.y + h[6] * zr;
    float scale = h[10] / (1.0f - depth * h[8]);
    return float2((h[0] + h[2]) * 0.5f + scale * xr, (h[1] + h[3]) * 0.5f - scale * yr);
}
float2 wavePhysical(TransformRecord t, float2 p) { return applyForward(t, p); }
float2 waveUnit(float2 v) { return length(v) > 1e-12f ? normalize(v) : float2(1.0f, 0.0f); }
float2 waveCrest(device const float *h, texture2d<float> source, TransformRecord t,
                  uint phase, float morph, int voice, thread float &depth) {
    uint phases = as_type<uint>(h[17]);
    float x = -1.0f + 2.0f * float(phase) / float(phases - 1u);
    float amplitude = waveAmplitude(h, source, 2.0f * M_PI_F * float(phase) / float(phases - 1u), morph, voice);
    return wavePhysical(t, waveProject(h, float3(x, amplitude, 2.0f * morph - 1.0f), depth));
}
constant uint waveQuad[6] = {0u, 1u, 2u, 1u, 3u, 2u};
struct WavetableLineData {
    uint count;
    float radius;
    float bounds[4];
    float2 points[256];
    float depth[256];
    float4 chunks[16];
};
vertex WavetableVertex wavetable3dVertex(uint vertexId [[vertex_id]], uint instanceId [[instance_id]],
                                         device const PrimitiveDesc *primitives [[buffer(0)]],
                                         device const float *payload [[buffer(1)]],
                                         device const TransformRecord *transforms [[buffer(2)]],
                                         constant FrameUniforms &uniforms [[buffer(4)]],
                                         constant uint &primitiveIndex [[buffer(8)]],
                                         constant uint &drawMode [[buffer(9)]],
                                         constant WavetableLineData &line [[buffer(10)]],
                                         array<texture2d<float>, 16> textures [[texture(1)]]) {
    PrimitiveDesc d = primitives[primitiveIndex];
    device const float *h = payload + d.payloadOffset;
    TransformRecord t = transforms[d.transformId];
    WavetableVertex out;
    out.primitive = primitiveIndex; out.mode = 0u; out.flags = 0u;
    out.data = float4(0.0f); out.normal = float2(0.0f);
    float2 p = float2(0.0f);
    if (drawMode == 3u) {
        uint corner = waveQuad[vertexId % 6u];
        p = float2((corner & 1u) != 0u ? line.bounds[2] : line.bounds[0],
                   corner >= 2u ? line.bounds[3] : line.bounds[1]);
        out.mode = 3u; out.flags = 1u;
    } else if (drawMode != 1u) {
        uint edge = vertexId / 6u, corner = waveQuad[vertexId % 6u];
        uint axis = edge / 4u, other = edge % 4u, bit = 0u;
        float3 a, b;
        for (uint k = 0; k < 3u; ++k) {
            if (k == axis) { a[k] = -1.0f; b[k] = 1.0f; }
            else { a[k] = b[k] = (other & (1u << bit++)) != 0u ? 1.0f : -1.0f; }
        }
        float za, zb;
        float2 pa = wavePhysical(t, waveProject(h, a, za)), pb = wavePhysical(t, waveProject(h, b, zb));
        bool front = za + zb >= 0.0f;
        if (front != (drawMode == 2u)) { out.position = float4(0.0f, 0.0f, 0.0f, 1.0f); return out; }
        float2 tangent = waveUnit(pb - pa), normal = float2(-tangent.y, tangent.x);
        float side = (corner & 1u) != 0u ? 1.0f : -1.0f;
        p = (corner >= 2u ? pb : pa) + side * 1.5f * normal;
        out.normal = normal; out.data.y = side * 1.5f; out.mode = 2u;
    } else {
        uint phases = as_type<uint>(h[17]);
        bool active; int voice;
        float morph = waveSlice(h, instanceId, active, voice);
        out.flags = (active ? 1u : 0u) | (voice >= 0 ? 2u : 0u);
        if (active && vertexId >= 2u * phases) { out.position = float4(0.0f, 0.0f, 0.0f, 1.0f); return out; }
        float focus = h[15] + (1.0f - h[15]) * exp(-pow(morph - h[12], 2.0f) / (2.0f * 0.14f * 0.14f));
        uint phase = (vertexId % (2u * phases)) / 2u;
        float depth;
        if (vertexId < 2u * phases) {
            bool baseline = (vertexId & 1u) != 0u;
            if (baseline) {
                float x = -1.0f + 2.0f * float(phase) / float(phases - 1u);
                float baseY = h[13];
                if ((as_type<uint>(h[19]) & 2u) != 0u) {
                    float amplitude = waveAmplitude(h, textures[min(d.auxHandle, 15u)],
                                                    2.0f * M_PI_F * float(phase) / float(phases - 1u), morph, voice);
                    baseY = amplitude - h[13];
                }
                p = wavePhysical(t, waveProject(h, float3(x, baseY, 2.0f * morph - 1.0f), depth));
            } else { p = waveCrest(h, textures[min(d.auxHandle, 15u)], t, phase, morph, voice, depth); }
            out.data.x = baseline ? 1.0f : 0.0f;
        } else {
            p = waveCrest(h, textures[min(d.auxHandle, 15u)], t, phase, morph, voice, depth);
            float ignored;
            float2 previous = waveCrest(h, textures[min(d.auxHandle, 15u)], t, phase == 0u ? 0u : phase - 1u, morph, voice, ignored);
            float2 next = waveCrest(h, textures[min(d.auxHandle, 15u)], t, min(phase + 1u, phases - 1u), morph, voice, ignored);
            float2 before = waveUnit(p - previous), after = waveUnit(next - p);
            float2 tangent = phase == 0u ? after : phase + 1u == phases ? before : waveUnit(before + after);
            float2 normal = float2(-tangent.y, tangent.x), segment = phase + 1u == phases ? before : after;
            float2 segmentNormal = float2(-segment.y, segment.x);
            bool studio = (as_type<uint>(h[19]) & 4u) != 0u;
            float radius = active ? (studio ? 0.95f + 2.5f * 1.3f : 0.95f + 3.0f * 2.6f)
                                  : (studio ? 0.6f + 1.0f : 0.55f + 1.0f);
            float extent = radius / max(0.25f, abs(dot(normal, segmentNormal)));
            float side = (vertexId & 1u) != 0u ? 1.0f : -1.0f;
            p += side * normal * extent;
            out.data.y = side * radius; out.normal = normal; out.mode = 1u;
        }
        out.data.z = focus; out.data.w = depth;
    }
    p -= uniforms.origin;
    out.position = float4(p.x / uniforms.viewport.x * 2.0f - 1.0f, 1.0f - p.y / uniforms.viewport.y * 2.0f,
                           1.0f - float(d.depth) / float(uniforms.maxDepth + 1u), 1.0f);
    return out;
}
float3 wavePalette(texture2d<float> gradients, uint row, float energy) {
    float p = clamp(energy, 0.0f, 1.0f) * 255.0f;
    uint a = uint(p), b = min(a + 1u, 255u);
    return mix(gradients.read(uint2(a, row)).rgb, gradients.read(uint2(b, row)).rgb, p - float(a));
}
fragment float4 wavetable3dFragment(WavetableVertex in [[stage_in]],
                                     device const PrimitiveDesc *primitives [[buffer(0)]],
                                     device const float *payload [[buffer(1)]],
                                     device const TransformRecord *transforms [[buffer(2)]],
                                     device const ClipRecord *clips [[buffer(3)]],
                                     texture2d<float> gradients [[texture(0)]],
                                     constant WavetableLineData &line [[buffer(10)]],
                                     constant FrameUniforms &uniforms [[buffer(4)]]) {
    PrimitiveDesc d = primitives[in.primitive];
    device const float *h = payload + d.payloadOffset;
    float2 pixel = in.position.xy + uniforms.origin;
    float2 local = applyInverse(transforms[d.transformId], pixel);
    if (local.x < h[0] || local.x > h[2] || local.y < h[1] || local.y > h[3]) { return float4(0.0f); }
    bool active = (in.flags & 1u) != 0u, ghost = (in.flags & 2u) != 0u;
    bool studio = (as_type<uint>(h[19]) & 4u) != 0u;
    float depth = exp(-0.45f * clamp(0.5f * (h[11] - in.data.w), 0.0f, 1.0f));
    if (in.mode == 3u) {
        float best = INFINITY, lineDepth = 0.0f;
        float2 closest = float2(0.0f), tangent = float2(1.0f, 0.0f);
        for (uint c = 0; c < 16u; ++c) {
            float4 box = line.chunks[c];
            if (pixel.x < box.x || pixel.y < box.y || pixel.x > box.z || pixel.y > box.w) { continue; }
            uint last = min(c * 16u + 16u, line.count - 1u);
            for (uint i = c * 16u; i < last; ++i) {
                float2 a = line.points[i], ab = line.points[i + 1u] - a;
                float length2 = dot(ab, ab);
                float t = length2 > 0.0f ? clamp(dot(pixel - a, ab) / length2, 0.0f, 1.0f) : 0.0f;
                float2 q = a + ab * t, delta = pixel - q;
                float distance2 = dot(delta, delta);
                if (distance2 < best) {
                    best = distance2; closest = q; tangent = ab;
                    lineDepth = line.depth[i] + (line.depth[i + 1u] - line.depth[i]) * t;
                }
            }
        }
        float distance = sqrt(best);
        if (!(distance <= line.radius)) { return float4(0.0f); }
        float2 direction = waveUnit(tangent);
        in.data.y = distance;
        in.normal = distance > 1e-9f ? (pixel - closest) / distance : float2(-direction.y, direction.x);
        in.mode = 1u;
        depth = exp(-0.45f * clamp(0.5f * (h[11] - lineDepth), 0.0f, 1.0f));
    }
    float4 result;
    if (in.mode == 0u) {
        float v = clamp(in.data.x, 0.0f, 1.0f);
        if (studio) {
            float alpha = (active ? 0.68f : 0.56f) * (0.65f + 0.35f * in.data.z) *
                          (0.75f + 0.25f * depth) * (1.0f - 0.35f * v) * (ghost ? 0.35f : 1.0f);
            result = float4(wavePalette(gradients, d.gradientOffset, 0.26f + 0.12f * in.data.z * (1.0f - v)) * alpha, alpha);
        } else {
            float alpha = (active ? 0.34f : 0.17f) * in.data.z * depth * exp(-2.4f * v) * pow(1.0f - v, 0.45f) * (ghost ? 0.35f : 1.0f);
            result = float4(wavePalette(gradients, d.gradientOffset, 0.32f + 0.36f * in.data.z * (1.0f - v)) * alpha, alpha);
        }
    } else {
        float width = in.mode == 2u ? (studio ? 0.85f : 1.0f)
                      : active      ? 1.9f
                                    : (studio ? 1.25f : 1.1f);
        float core = boxCoverage(abs(in.data.y) - 0.5f * width, in.normal);
        if (in.mode == 2u) {
            float alpha = (studio ? 0.68f : 0.55f) * core;
            result = studio ? float4(float3(0.036f) * alpha, alpha)
                            : float4(float3(0.0273209f, 0.0318960f, 0.0423114f) * alpha, alpha);
        } else if (active) {
            float sigma = studio ? 1.3f : 2.6f;
            float halo = exp(-in.data.y * in.data.y / (2.0f * sigma * sigma));
            float3 hot = wavePalette(gradients, d.gradientOffset, studio ? 1.0f : 0.98f);
            float3 warm = wavePalette(gradients, d.gradientOffset, studio ? 0.65f : 0.58f);
            float atten = studio ? (0.88f + 0.12f * depth) : depth;
            float haloGain = studio ? 0.18f : 0.42f;
            result = float4((hot * core + haloGain * warm * halo) * atten, core * atten);
        } else {
            float alpha = studio ? 0.90f * (0.70f + 0.30f * in.data.z) * (0.78f + 0.22f * depth) * core * (ghost ? 0.35f : 1.0f)
                                 : 0.75f * in.data.z * depth * core * (ghost ? 0.35f : 1.0f);
            result = float4(wavePalette(gradients, d.gradientOffset,
                                        studio ? (0.52f + 0.22f * in.data.z) : (0.30f + 0.40f * in.data.z)) * alpha, alpha);
        }
    }
    FrameBuffers frame; frame.primitives = primitives; frame.payload = payload; frame.transforms = transforms; frame.clips = clips;
    return outputColor(result * clipCoverage(frame, d.clipId, pixel), uniforms.outputColorSpace);
}
