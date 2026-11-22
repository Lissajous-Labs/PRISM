struct VertexOut {
    float4 position [[position]];
    uint primitive [[flat]];
};

struct LcdOut {
    float4 color [[color(0), index(0)]];
    float4 coverage [[color(0), index(1)]];
};

constant float2 quadCorners[6] = {float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(0.0f, 1.0f),
                                  float2(1.0f, 0.0f), float2(1.0f, 1.0f), float2(0.0f, 1.0f)};

vertex VertexOut uberVertex(uint vertexId [[vertex_id]], uint instanceId [[instance_id]],
                            device const PrimitiveDesc *primitives [[buffer(0)]],
                            device const float *payload [[buffer(1)]],
                            device const TransformRecord *transforms [[buffer(2)]],
                            constant FrameUniforms &uniforms [[buffer(4)]],
                            device const uint *indices [[buffer(5)]]) {
    uint index = uniforms.indexed != 0u ? indices[instanceId] : instanceId;
    PrimitiveDesc d = primitives[index];
    float2 corner = quadCorners[vertexId];
    float2 local = float2(mix(d.bounds[0], d.bounds[2], corner.x), mix(d.bounds[1], d.bounds[3], corner.y));
    if ((d.kindFlags & kindMask) == kindCapsule) {
        float2 a = float2(payload[d.payloadOffset], payload[d.payloadOffset + 1]);
        float2 b = float2(payload[d.payloadOffset + 2], payload[d.payloadOffset + 3]);
        float reach = d.params[0] + 1.5f;
        float2 axis = length(b - a) > 1e-6f ? normalize(b - a) : float2(1.0f, 0.0f);
        float2 normal = float2(-axis.y, axis.x);
        float2 along = mix(a - axis * reach, b + axis * reach, corner.x);
        local = along + normal * reach * (2.0f * corner.y - 1.0f);
    }
    float2 physical = applyForward(transforms[d.transformId], local) - uniforms.origin;
    VertexOut out;
    out.position = float4(physical.x / uniforms.viewport.x * 2.0f - 1.0f, 1.0f - physical.y / uniforms.viewport.y * 2.0f,
                          1.0f - float(d.depth) / float(uniforms.maxDepth + 1u), 1.0f);
    out.primitive = index;
    return out;
}

vertex VertexOut uberCoreVertex(uint vertexId [[vertex_id]], uint instanceId [[instance_id]],
                                device const PrimitiveDesc *primitives [[buffer(0)]],
                                device const float *payload [[buffer(1)]],
                                device const TransformRecord *transforms [[buffer(2)]],
                                constant FrameUniforms &uniforms [[buffer(4)]],
                                device const uint *indices [[buffer(5)]]) {
    uint index = indices[instanceId];
    PrimitiveDesc d = primitives[index];
    uint offset = d.payloadOffset;
    float inset = max(max(d.params[0], d.params[1]), max(d.params[2], d.params[3])) * 0.29289322f + 1.0f + aaMargin;
    float4 edges = float4(inset);
    if ((d.kindFlags & borderFlag) != 0u) {
        edges += float4(payload[offset], payload[offset + 1], payload[offset + 2], payload[offset + 3]);
    }
    float2 low = float2(d.bounds[0] + edges.x, d.bounds[1] + edges.y);
    float2 high = max(float2(d.bounds[2] - edges.z, d.bounds[3] - edges.w), low);
    float2 corner = quadCorners[vertexId];
    float2 physical = applyForward(transforms[d.transformId], mix(low, high, corner)) - uniforms.origin;
    VertexOut out;
    out.position = float4(physical.x / uniforms.viewport.x * 2.0f - 1.0f, 1.0f - physical.y / uniforms.viewport.y * 2.0f,
                          1.0f - float(d.depth) / float(uniforms.maxDepth + 1u), 1.0f);
    out.primitive = index;
    return out;
}

ShadeOut shadeFragment(VertexOut in, device const PrimitiveDesc *primitives, device const float *payload,
                       device const TransformRecord *transforms, device const ClipRecord *clips,
                       device const float4 *halftoneCells, device const ShadowSprite *shadowTable,
                       device const float *shadowSprites, texture2d<float> gradients,
                       array<texture2d<float>, 24> textures, constant FrameUniforms &uniforms) {
    FrameBuffers frame;
    frame.primitives = primitives;
    frame.payload = payload;
    frame.transforms = transforms;
    frame.clips = clips;
    frame.halftoneCells = halftoneCells;
    frame.shadowTable = shadowTable;
    frame.shadowSprites = shadowSprites;
    PrimitiveDesc d = primitives[in.primitive];
    uint slot = d.auxHandle == noTexture ? 0u : min(d.auxHandle, 23u);
    return shadePrimitive(frame, d, gradients, textures[slot], in.position.xy + uniforms.origin);
}

[[early_fragment_tests]] fragment float4 uberCoreFragment(VertexOut in [[stage_in]], device const PrimitiveDesc *primitives [[buffer(0)]]) {
    return decodeColor(primitives[in.primitive].color);
}

[[early_fragment_tests]] fragment float4 uberFragment(VertexOut in [[stage_in]], device const PrimitiveDesc *primitives [[buffer(0)]],
                             device const float *payload [[buffer(1)]], device const TransformRecord *transforms [[buffer(2)]],
                             device const ClipRecord *clips [[buffer(3)]], device const float4 *halftoneCells [[buffer(7)]],
                             device const ShadowSprite *shadowTable [[buffer(8)]], device const float *shadowSprites [[buffer(9)]],
                             texture2d<float> gradients [[texture(0)]], array<texture2d<float>, 24> textures [[texture(1)]],
                             constant FrameUniforms &uniforms [[buffer(4)]]) {
    return shadeFragment(in, primitives, payload, transforms, clips, halftoneCells, shadowTable, shadowSprites,
                         gradients, textures, uniforms).color;
}

[[early_fragment_tests]] fragment float4 uberCountingFragment(VertexOut in [[stage_in]],
                                   device const PrimitiveDesc *primitives [[buffer(0)]],
                                   device const float *payload [[buffer(1)]], device const TransformRecord *transforms [[buffer(2)]],
                                   device const ClipRecord *clips [[buffer(3)]], device atomic_uint *counter [[buffer(6)]],
                                   device const float4 *halftoneCells [[buffer(7)]],
                                   device const ShadowSprite *shadowTable [[buffer(8)]], device const float *shadowSprites [[buffer(9)]],
                                   texture2d<float> gradients [[texture(0)]], array<texture2d<float>, 24> textures [[texture(1)]],
                                   constant FrameUniforms &uniforms [[buffer(4)]]) {
    atomic_fetch_add_explicit(counter, 1u, memory_order_relaxed);
    return shadeFragment(in, primitives, payload, transforms, clips, halftoneCells, shadowTable, shadowSprites,
                         gradients, textures, uniforms).color;
}

[[early_fragment_tests]] fragment float4 uberMultiplyFragment(VertexOut in [[stage_in]], float4 destination [[color(0)]],
                                     device const PrimitiveDesc *primitives [[buffer(0)]],
                                     device const float *payload [[buffer(1)]], device const TransformRecord *transforms [[buffer(2)]],
                                     device const ClipRecord *clips [[buffer(3)]], device const float4 *halftoneCells [[buffer(7)]],
                                     device const ShadowSprite *shadowTable [[buffer(8)]], device const float *shadowSprites [[buffer(9)]],
                                     texture2d<float> gradients [[texture(0)]], array<texture2d<float>, 24> textures [[texture(1)]],
                                     constant FrameUniforms &uniforms [[buffer(4)]]) {
    float4 source = shadeFragment(in, primitives, payload, transforms, clips, halftoneCells, shadowTable, shadowSprites,
                                  gradients, textures, uniforms).color;
    float3 rgb = source.rgb * destination.rgb + (1.0f - destination.a) * source.rgb + (1.0f - source.a) * destination.rgb;
    return float4(rgb, source.a + destination.a * (1.0f - source.a));
}

[[early_fragment_tests]] fragment LcdOut uberLcdFragment(VertexOut in [[stage_in]], device const PrimitiveDesc *primitives [[buffer(0)]],
                                device const float *payload [[buffer(1)]], device const TransformRecord *transforms [[buffer(2)]],
                                device const ClipRecord *clips [[buffer(3)]], device const float4 *halftoneCells [[buffer(7)]],
                                device const ShadowSprite *shadowTable [[buffer(8)]], device const float *shadowSprites [[buffer(9)]],
                                texture2d<float> gradients [[texture(0)]], array<texture2d<float>, 24> textures [[texture(1)]],
                                constant FrameUniforms &uniforms [[buffer(4)]]) {
    ShadeOut shaded = shadeFragment(in, primitives, payload, transforms, clips, halftoneCells, shadowTable, shadowSprites,
                                    gradients, textures, uniforms);
    LcdOut out;
    out.color = shaded.color;
    out.coverage = float4(shaded.coverage, shaded.color.a);
    return out;
}

struct ClearOut {
    float4 position [[position]];
};

vertex ClearOut clearVertex(uint vertexId [[vertex_id]]) {
    float2 corner = float2(float((vertexId << 1) & 2u), float(vertexId & 2u));
    ClearOut out;
    out.position = float4(corner * 2.0f - 1.0f, 1.0f, 1.0f);
    return out;
}

fragment float4 clearFragment(ClearOut in [[stage_in]], constant float4 &color [[buffer(0)]]) {
    return color;
}

kernel void reflectLayout(device const PrimitiveDesc *input [[buffer(0)]], device uint *output [[buffer(1)]]) {
    PrimitiveDesc d = input[0];
    output[0] = as_type<uint>(d.bounds[0]);
    output[1] = as_type<uint>(d.bounds[3]);
    output[2] = d.kindFlags;
    output[3] = d.color;
    output[4] = d.borderColor;
    output[5] = d.depth;
    output[6] = uint(d.transformId);
    output[7] = uint(d.clipId);
    output[8] = d.payloadOffset;
    output[9] = as_type<uint>(d.params[0]);
    output[10] = as_type<uint>(d.params[3]);
    output[11] = d.auxHandle;
    output[12] = d.gradientOffset;
    output[13] = sizeof(PrimitiveDesc);
    output[14] = sizeof(TransformRecord);
    output[15] = sizeof(ClipRecord);
}
