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
                            device const TransformRecord *transforms [[buffer(2)]],
                            constant FrameUniforms &uniforms [[buffer(4)]],
                            device const uint *indices [[buffer(5)]]) {
    uint index = indices[instanceId];
    PrimitiveDesc d = primitives[index];
    float2 corner = quadCorners[vertexId];
    float2 local = float2(mix(d.bounds[0], d.bounds[2], corner.x), mix(d.bounds[1], d.bounds[3], corner.y));
    float2 physical = applyForward(transforms[d.transformId], local);
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
    float inset = max(max(d.params[0], d.params[1]), max(d.params[2], d.params[3])) + 1.0f;
    float4 edges = float4(inset);
    if ((d.kindFlags & borderFlag) != 0u) {
        edges += float4(payload[offset + 4], payload[offset + 5], payload[offset + 6], payload[offset + 7]);
    }
    float2 low = float2(payload[offset] + edges.x, payload[offset + 1] + edges.y);
    float2 high = max(float2(payload[offset + 2] - edges.z, payload[offset + 3] - edges.w), low);
    float2 corner = quadCorners[vertexId];
    float2 physical = applyForward(transforms[d.transformId], mix(low, high, corner));
    VertexOut out;
    out.position = float4(physical.x / uniforms.viewport.x * 2.0f - 1.0f, 1.0f - physical.y / uniforms.viewport.y * 2.0f,
                          1.0f - float(d.depth) / float(uniforms.maxDepth + 1u), 1.0f);
    out.primitive = index;
    return out;
}

ShadeOut shadeFragment(VertexOut in, device const PrimitiveDesc *primitives, device const float *payload,
                       device const TransformRecord *transforms, device const ClipRecord *clips,
                       device const float4 *halftoneCells, texture2d<float> gradients,
                       array<texture2d<float>, 24> textures) {
    FrameBuffers frame;
    frame.primitives = primitives;
    frame.payload = payload;
    frame.transforms = transforms;
    frame.clips = clips;
    frame.halftoneCells = halftoneCells;
    PrimitiveDesc d = primitives[in.primitive];
    uint slot = d.auxHandle == noTexture ? 0u : min(d.auxHandle, 23u);
    return shadePrimitive(frame, d, gradients, textures[slot], in.position.xy);
}

fragment float4 uberFragment(VertexOut in [[stage_in]], device const PrimitiveDesc *primitives [[buffer(0)]],
                             device const float *payload [[buffer(1)]], device const TransformRecord *transforms [[buffer(2)]],
                             device const ClipRecord *clips [[buffer(3)]], device const float4 *halftoneCells [[buffer(7)]],
                             texture2d<float> gradients [[texture(0)]], array<texture2d<float>, 24> textures [[texture(1)]]) {
    return shadeFragment(in, primitives, payload, transforms, clips, halftoneCells, gradients, textures).color;
}

[[early_fragment_tests]] fragment float4 uberCountingFragment(VertexOut in [[stage_in]],
                                   device const PrimitiveDesc *primitives [[buffer(0)]],
                                   device const float *payload [[buffer(1)]], device const TransformRecord *transforms [[buffer(2)]],
                                   device const ClipRecord *clips [[buffer(3)]], device atomic_uint *counter [[buffer(6)]],
                                   device const float4 *halftoneCells [[buffer(7)]],
                                   texture2d<float> gradients [[texture(0)]], array<texture2d<float>, 24> textures [[texture(1)]]) {
    atomic_fetch_add_explicit(counter, 1u, memory_order_relaxed);
    return shadeFragment(in, primitives, payload, transforms, clips, halftoneCells, gradients, textures).color;
}

fragment float4 uberMultiplyFragment(VertexOut in [[stage_in]], float4 destination [[color(0)]],
                                     device const PrimitiveDesc *primitives [[buffer(0)]],
                                     device const float *payload [[buffer(1)]], device const TransformRecord *transforms [[buffer(2)]],
                                     device const ClipRecord *clips [[buffer(3)]], device const float4 *halftoneCells [[buffer(7)]],
                                     texture2d<float> gradients [[texture(0)]], array<texture2d<float>, 24> textures [[texture(1)]]) {
    float4 source = shadeFragment(in, primitives, payload, transforms, clips, halftoneCells, gradients, textures).color;
    float3 rgb = source.rgb * destination.rgb + (1.0f - destination.a) * source.rgb + (1.0f - source.a) * destination.rgb;
    return float4(rgb, source.a + destination.a * (1.0f - source.a));
}

fragment LcdOut uberLcdFragment(VertexOut in [[stage_in]], device const PrimitiveDesc *primitives [[buffer(0)]],
                                device const float *payload [[buffer(1)]], device const TransformRecord *transforms [[buffer(2)]],
                                device const ClipRecord *clips [[buffer(3)]], device const float4 *halftoneCells [[buffer(7)]],
                                texture2d<float> gradients [[texture(0)]], array<texture2d<float>, 24> textures [[texture(1)]]) {
    ShadeOut shaded = shadeFragment(in, primitives, payload, transforms, clips, halftoneCells, gradients, textures);
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
