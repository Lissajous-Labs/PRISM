#include "backends/metal/MetalSurfaceState.h"
#include "backends/metal/MetalUpload.h"
#include "backends/metal/MetalWavetable3d.h"

namespace prism::backends {
void MetalSurfaceState::createTargets() {
    queue = [device->device newCommandQueue];
    MTLTextureDescriptor *colorDescriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                           width:extent.width
                                                          height:extent.height
                                                       mipmapped:NO];
    colorDescriptor.usage =
        MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    colorDescriptor.storageMode = MTLStorageModePrivate;
    color = [device->device newTextureWithDescriptor:colorDescriptor];
    MTLTextureDescriptor *depthDescriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                           width:extent.width
                                                          height:extent.height
                                                       mipmapped:NO];
    depthDescriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    depthDescriptor.storageMode = MTLStorageModePrivate;
    depth = [device->device newTextureWithDescriptor:depthDescriptor];
    counter = [device->device newBufferWithLength:sizeof(std::uint32_t)
                                          options:MTLResourceStorageModeShared];
}

void MetalSurfaceState::releaseTargets() {
    queue = nil;
    color = nil;
    depth = nil;
    scratchColor = nil;
    scratchDepth = nil;
}

void MetalSurfaceState::encodeScratch(id<MTLCommandBuffer> commands, FrameUniforms uniforms,
                                      const float *clear) {
    constexpr std::int32_t shelfWidth = 4096;
    scratchSlots.clear();
    std::int32_t x = 0, y = 0, rowHeight = 0, width = 1;
    for (const hal::Rect &rect : damageRects) {
        const std::int32_t w = rect.x1 - rect.x0, h = rect.y1 - rect.y0;
        if (x + w > shelfWidth && x > 0) {
            y += rowHeight;
            x = 0;
            rowHeight = 0;
        }
        scratchSlots.push_back({x, y});
        x += w;
        rowHeight = std::max(rowHeight, h);
        width = std::max(width, x);
    }
    const auto needWidth = static_cast<NSUInteger>(width);
    const auto needHeight = static_cast<NSUInteger>(std::max(y + rowHeight, 1));
    if (scratchColor == nil || scratchColor.width < needWidth || scratchColor.height < needHeight) {
        const NSUInteger w = std::max<NSUInteger>(needWidth, scratchColor ? scratchColor.width : 0);
        const NSUInteger h =
            std::max<NSUInteger>(needHeight, scratchColor ? scratchColor.height : 0);
        MTLTextureDescriptor *colorDescriptor =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                               width:w
                                                              height:h
                                                           mipmapped:NO];
        colorDescriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        colorDescriptor.storageMode = MTLStorageModePrivate;
        scratchColor = [device->device newTextureWithDescriptor:colorDescriptor];
        MTLTextureDescriptor *depthDescriptor =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                               width:w
                                                              height:h
                                                           mipmapped:NO];
        depthDescriptor.usage = MTLTextureUsageRenderTarget;
        depthDescriptor.storageMode = MTLStorageModePrivate;
        scratchDepth = [device->device newTextureWithDescriptor:depthDescriptor];
    }
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = scratchColor;
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.depthAttachment.texture = scratchDepth;
    pass.depthAttachment.loadAction = MTLLoadActionDontCare;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    uniforms.viewport[0] = static_cast<float>(scratchColor.width);
    uniforms.viewport[1] = static_cast<float>(scratchColor.height);
    uniforms.indexed = 1;
    const auto *descriptors = static_cast<const render::PrimitiveDesc *>(primitives.contents);
    const auto overlaps = [&](std::uint32_t index, const hal::Rect &rect) {
        const hal::Rect &b = primitiveBounds[index];
        return b.x0 < rect.x1 && b.x1 > rect.x0 && b.y0 < rect.y1 && b.y1 > rect.y0;
    };
    scratchIndices.clear();
    scratchRanges.clear();
    for (const hal::Rect &rect : damageRects) {
        const auto opaqueFirst = static_cast<std::uint32_t>(scratchIndices.size());
        for (const std::uint32_t index : batches.opaqueIndices()) {
            if (overlaps(index, rect)) {
                scratchIndices.push_back(index);
            }
        }
        const auto orderedFirst = static_cast<std::uint32_t>(scratchIndices.size());
        for (const std::uint32_t index : batches.ordered()) {
            if (overlaps(index, rect)) {
                scratchIndices.push_back(index);
            }
        }
        scratchRanges.push_back(
            {opaqueFirst, orderedFirst, static_cast<std::uint32_t>(scratchIndices.size())});
    }
    upload(device->device, scratchIndexBuffer, scratchIndices.data(),
           scratchIndices.size() * sizeof(std::uint32_t));
    uploadBytes += scratchIndices.size() * sizeof(std::uint32_t);
    id<MTLRenderCommandEncoder> encoder = beginPass(commands, pass, uniforms);
    [encoder setVertexBuffer:scratchIndexBuffer offset:0 atIndex:5];
    for (std::size_t i = 0; i < damageRects.size(); ++i) {
        const hal::Rect &rect = damageRects[i];
        const auto &slot = scratchSlots[i];
        const auto &range = scratchRanges[i];
        uniforms.origin[0] = static_cast<float>(rect.x0 - slot[0]);
        uniforms.origin[1] = static_cast<float>(rect.y0 - slot[1]);
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
        [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];
        [encoder setScissorRect:MTLScissorRect{static_cast<NSUInteger>(slot[0]),
                                               static_cast<NSUInteger>(slot[1]),
                                               static_cast<NSUInteger>(rect.x1 - rect.x0),
                                               static_cast<NSUInteger>(rect.y1 - rect.y0)}];
        [encoder setRenderPipelineState:device->clear];
        [encoder setDepthStencilState:device->depthReset];
        [encoder setFragmentBytes:clear length:4 * sizeof(float) atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder setFragmentBuffer:primitives offset:0 atIndex:0];
        ++drawCalls;
        if (range.ordered > range.opaque) {
            [encoder setRenderPipelineState:counting ? device->countingCore : device->opaque];
            [encoder setDepthStencilState:device->depthWrite];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:6
                      instanceCount:range.ordered - range.opaque
                       baseInstance:range.opaque];
            ++drawCalls;
        }
        [encoder setDepthStencilState:device->depthTest];
        std::uint32_t cursor = range.ordered;
        while (cursor < range.end) {
            const auto index = scratchIndices[cursor];
            if (descriptors[index].kind() == render::PrimitiveKind::wavetable3d) {
                drawWavetable3d(encoder, *device, descriptors[index], index, wavetableLine(index));
                [encoder setVertexBuffer:scratchIndexBuffer offset:0 atIndex:5];
                ++drawCalls;
                ++cursor;
                continue;
            }
            const auto pipelineKind = render::pipelineFor(descriptors[index]);
            const auto begin = cursor++;
            while (cursor < range.end &&
                   descriptors[scratchIndices[cursor]].kind() !=
                       render::PrimitiveKind::wavetable3d &&
                   render::pipelineFor(descriptors[scratchIndices[cursor]]) == pipelineKind) {
                ++cursor;
            }
            id<MTLRenderPipelineState> pipeline =
                counting && render::blendsSourceOver(pipelineKind)
                    ? device->counting
                    : device->pipelines[static_cast<std::size_t>(pipelineKind)];
            if (pipeline == nil) {
                continue;
            }
            [encoder setRenderPipelineState:pipeline];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:6
                      instanceCount:cursor - begin
                       baseInstance:begin];
            ++drawCalls;
        }
    }
    [encoder endEncoding];
    id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
    for (std::size_t i = 0; i < damageRects.size(); ++i) {
        const hal::Rect &rect = damageRects[i];
        const auto &slot = scratchSlots[i];
        [blit copyFromTexture:scratchColor
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(static_cast<NSUInteger>(slot[0]),
                                            static_cast<NSUInteger>(slot[1]), 0)
                   sourceSize:MTLSizeMake(static_cast<NSUInteger>(rect.x1 - rect.x0),
                                          static_cast<NSUInteger>(rect.y1 - rect.y0), 1)
                    toTexture:color
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(static_cast<NSUInteger>(rect.x0),
                                            static_cast<NSUInteger>(rect.y0), 0)];
    }
    [blit endEncoding];
}

void MetalSurfaceState::uploadFrame(const render::FrameView &view) {
    id<MTLDevice> gpu = device->device;
    upload(gpu, primitives, view.primitives.data(), view.primitives.size_bytes());
    upload(gpu, payload, view.payload.data(), view.payload.size_bytes());
    upload(gpu, transforms, view.transforms.data(), view.transforms.size_bytes());
    upload(gpu, clips, view.clips.data(), view.clips.size_bytes());
    batches.build(view.primitives, earlyDepth && view.effects.empty());
    const auto opaque = batches.opaqueIndices();
    upload(gpu, indices, opaque.data(), opaque.size_bytes());
    uploadBytes = view.primitives.size_bytes() + view.payload.size_bytes() +
                  view.transforms.size_bytes() + view.clips.size_bytes() + opaque.size_bytes() +
                  view.gradientRows.size() * 2;
    drawCalls = 0;
    primitiveBounds.resize(view.primitives.size());
    for (std::size_t i = 0; i < view.primitives.size(); ++i) {
        const auto &d = view.primitives[i];
        const auto &t = view.transforms[d.transformId];
        float x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
        for (const float x : {d.bounds[0], d.bounds[2]}) {
            for (const float y : {d.bounds[1], d.bounds[3]}) {
                const float px = t.forward[0] * x + t.forward[2] * y + t.forward[4];
                const float py = t.forward[1] * x + t.forward[3] * y + t.forward[5];
                x0 = std::min(x0, px);
                y0 = std::min(y0, py);
                x1 = std::max(x1, px);
                y1 = std::max(y1, py);
            }
        }
        primitiveBounds[i] = {static_cast<std::int32_t>(std::floor(x0)) - 1,
                              static_cast<std::int32_t>(std::floor(y0)) - 1,
                              static_cast<std::int32_t>(std::ceil(x1)) + 1,
                              static_cast<std::int32_t>(std::ceil(y1)) + 1};
    }
    const auto rows =
        static_cast<std::uint32_t>(view.gradientRows.size() / (render::gradientRowTexels * 4));
    gradients = rows ? uploadTexture(gpu, gradients, render::gradientRowTexels, rows, 4,
                                     view.gradientRows, texelScratch, hal::PixelFormat::rgba16Float)
                     : nil;
    for (std::size_t slot = 0; slot < textureSlots; ++slot) {
        if (slot < view.textures.size()) {
            const auto &texture = view.textures[slot];
            const auto &previous = uploadedTextures[slot];
            if (texture.nativeTexture != nullptr) {
                textures[slot] = (__bridge id<MTLTexture>)texture.nativeTexture;
                uploadedTextures[slot] = texture;
                continue;
            }
            if (previous.nativeTexture != nullptr) {
                textures[slot] = nil;
            }
            if (textures[slot] == nil || texture.generation == 0 ||
                previous.generation != texture.generation ||
                previous.texels.data() != texture.texels.data() ||
                previous.width != texture.width || previous.height != texture.height ||
                previous.channels != texture.channels || previous.format != texture.format) {
                textures[slot] =
                    uploadTexture(gpu, textures[slot], texture.width, texture.height,
                                  texture.channels, texture.texels, texelScratch, texture.format);
                uploadedTextures[slot] = texture;
                uploadBytes += texture.texels.size_bytes();
            }
        } else {
            textures[slot] = nil;
        }
    }
    formants.prepare(*device, view, primitives, extent);
    halftones.prepare(*device, view, primitives, payload);
    shadows.prepare(*device, view, primitives);
    wavetableLines.clear();
    for (std::size_t i = 0; i < view.primitives.size(); ++i) {
        if (view.primitives[i].kind() == render::PrimitiveKind::wavetable3d) {
            render::WavetableLine line;
            if (render::wavetableActiveLine(view, view.primitives[i], line)) {
                wavetableLines.emplace_back(static_cast<std::uint32_t>(i), line);
            }
        }
    }
}

void MetalSurfaceState::bindUber(id<MTLRenderCommandEncoder> encoder,
                                 const FrameUniforms &uniforms) {
    [encoder setVertexBuffer:primitives offset:0 atIndex:0];
    [encoder setVertexBuffer:payload offset:0 atIndex:1];
    [encoder setVertexBuffer:transforms offset:0 atIndex:2];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];
    [encoder setVertexBuffer:indices offset:0 atIndex:5];
    [encoder setFragmentBuffer:primitives offset:0 atIndex:0];
    [encoder setFragmentBuffer:payload offset:0 atIndex:1];
    [encoder setFragmentBuffer:transforms offset:0 atIndex:2];
    [encoder setFragmentBuffer:clips offset:0 atIndex:3];
    [encoder setFragmentBuffer:counter offset:0 atIndex:6];
    [encoder setFragmentBuffer:halftones.cells() offset:0 atIndex:7];
    [encoder setFragmentBuffer:shadows.table() offset:0 atIndex:8];
    [encoder setFragmentBuffer:shadows.sprites() offset:0 atIndex:9];
    [encoder setFragmentTexture:(gradients ? gradients : device->emptyTexture) atIndex:0];
    for (std::size_t slot = 0; slot < textureSlots; ++slot) {
        [encoder setVertexTexture:(textures[slot] ? textures[slot] : device->emptyTexture)
                          atIndex:slot + 1];
        [encoder setFragmentTexture:(textures[slot] ? textures[slot] : device->emptyTexture)
                            atIndex:slot + 1];
    }
    for (std::size_t slot = 0; slot < formantSlots; ++slot) {
        const auto texture = formants.texture(slot);
        [encoder setFragmentTexture:(texture ? texture : device->emptyTexture)
                            atIndex:textureSlots + 1 + slot];
        [encoder setVertexTexture:(texture ? texture : device->emptyTexture)
                          atIndex:textureSlots + 1 + slot];
    }
}

id<MTLRenderCommandEncoder> MetalSurfaceState::beginPass(id<MTLCommandBuffer> commands,
                                                         MTLRenderPassDescriptor *pass,
                                                         const FrameUniforms &uniforms) {
    id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
    bindUber(encoder, uniforms);
    return encoder;
}

void MetalSurfaceState::drawOrdered(id<MTLRenderCommandEncoder> encoder, std::uint32_t from,
                                    std::uint32_t to) {
    [encoder setDepthStencilState:device->depthTest];
    for (const render::DrawRun &run : batches.runs()) {
        const std::uint32_t start = std::max(run.first, from);
        const std::uint32_t end = std::min(run.first + run.count, to);
        if (end <= start) {
            continue;
        }
        id<MTLRenderPipelineState> pipeline =
            device->pipelines[static_cast<std::size_t>(run.pipeline)];
        if (counting && render::blendsSourceOver(run.pipeline)) {
            pipeline = device->counting;
        }
        if (pipeline == nil) {
            continue;
        }
        const auto *descriptors = static_cast<const render::PrimitiveDesc *>(primitives.contents);
        std::uint32_t cursor = start;
        while (cursor < end) {
            const auto index = cursor;
            if (descriptors[index].kind() == render::PrimitiveKind::wavetable3d) {
                drawWavetable3d(encoder, *device, descriptors[index], index, wavetableLine(index));
                ++drawCalls;
                ++cursor;
                continue;
            }
            const auto begin = cursor++;
            while (cursor < end &&
                   descriptors[cursor].kind() != render::PrimitiveKind::wavetable3d) {
                ++cursor;
            }
            [encoder setRenderPipelineState:pipeline];
            ++drawCalls;
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:6
                      instanceCount:cursor - begin
                       baseInstance:begin];
        }
    }
}

void MetalSurfaceState::encodeFrame(id<MTLCommandBuffer> commands,
                                    const render::FrameRequest &request) {
    const auto primitiveCount = static_cast<std::uint32_t>(request.view.primitives.size());
    const FrameUniforms uniforms{
        {static_cast<float>(extent.width), static_cast<float>(extent.height)},
        primitiveCount,
        static_cast<std::uint32_t>(request.view.textures.size()),
        counting ? 1u : 0u,
        0,
        {0, 0}};
    formants.encode(*device, commands, primitives, payload, transforms);
    halftones.encode(*device, commands, primitives, payload, gradients, textures);
    shadows.encode(*device, commands);
    paths.prepare(*device, commands, request.view, extent);
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = color;
    pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.depthAttachment.texture = depth;
    pass.depthAttachment.loadAction = MTLLoadActionLoad;
    pass.depthAttachment.storeAction = MTLStoreActionStore;
    const float clear[4] = {
        static_cast<float>(request.clear.r), static_cast<float>(request.clear.g),
        static_cast<float>(request.clear.b), static_cast<float>(request.clear.a)};
    const hal::Rect viewport{0, 0, static_cast<std::int32_t>(extent.width),
                             static_cast<std::int32_t>(extent.height)};
    const PathFineTargets targets{color, depth, transforms, clips, primitiveCount};
    const auto &view = request.view;
    damageRects.clear();
    if (!view.effects.empty()) {
        damageRects.push_back(viewport);
    } else {
        request.damage->forEachRect([&](hal::Rect rect) {
            const hal::Rect clipped{std::max(rect.x0, viewport.x0), std::max(rect.y0, viewport.y0),
                                    std::min(rect.x1, viewport.x1), std::min(rect.y1, viewport.y1)};
            if (!clipped.empty()) {
                damageRects.push_back(clipped);
            }
        });
    }
    const auto scissor = [](id<MTLRenderCommandEncoder> encoder, hal::Rect rect) {
        [encoder setScissorRect:MTLScissorRect{static_cast<NSUInteger>(rect.x0),
                                               static_cast<NSUInteger>(rect.y0),
                                               static_cast<NSUInteger>(rect.x1 - rect.x0),
                                               static_cast<NSUInteger>(rect.y1 - rect.y0)}];
    };
    const auto drawRange = [&](id<MTLRenderCommandEncoder> encoder, std::uint32_t from,
                               std::uint32_t to) {
        for (const hal::Rect &rect : damageRects) {
            scissor(encoder, rect);
            drawOrdered(encoder, from, to);
        }
    };
    if (view.paths.empty() && view.effects.empty() && damageRects.size() > 1) {
        constexpr std::int64_t rectCost = 65536;
        const auto area = [](const hal::Rect &r) {
            return static_cast<std::int64_t>(r.x1 - r.x0) * (r.y1 - r.y0);
        };
        bool merged = true;
        while (merged) {
            merged = false;
            for (std::size_t a = 0; a < damageRects.size() && !merged; ++a) {
                for (std::size_t b = a + 1; b < damageRects.size(); ++b) {
                    const hal::Rect &p = damageRects[a], &q = damageRects[b];
                    const hal::Rect both{std::min(p.x0, q.x0), std::min(p.y0, q.y0),
                                         std::max(p.x1, q.x1), std::max(p.y1, q.y1)};
                    if (area(both) <= area(p) + area(q) + rectCost) {
                        damageRects[a] = both;
                        damageRects.erase(damageRects.begin() + static_cast<std::ptrdiff_t>(b));
                        merged = true;
                        break;
                    }
                }
            }
        }
    }
    std::int64_t damagedArea = 0;
    for (const hal::Rect &rect : damageRects) {
        damagedArea += static_cast<std::int64_t>(rect.x1 - rect.x0) * (rect.y1 - rect.y0);
    }
    const bool scratch = view.paths.empty() && view.effects.empty() && request.bloom == nullptr &&
                         !damageRects.empty() && damageRects.size() <= 256 &&
                         damagedArea * 2 < static_cast<std::int64_t>(extent.width) * extent.height;
    if (scratch) {
        encodeScratch(commands, uniforms, clear);
    } else if (!damageRects.empty()) {
        const bool fullFrame = damageRects.size() == 1 && damageRects[0].x0 == viewport.x0 &&
                               damageRects[0].y0 == viewport.y0 &&
                               damageRects[0].x1 == viewport.x1 && damageRects[0].y1 == viewport.y1;
        const bool split = !view.paths.empty() || !view.effects.empty();
        pass.colorAttachments[0].loadAction = fullFrame ? MTLLoadActionDontCare : MTLLoadActionLoad;
        pass.depthAttachment.loadAction = MTLLoadActionDontCare;
        pass.depthAttachment.storeAction = split ? MTLStoreActionStore : MTLStoreActionDontCare;
        id<MTLRenderCommandEncoder> encoder = beginPass(commands, pass, uniforms);
        const auto opaqueCount = static_cast<NSUInteger>(batches.opaqueIndices().size());
        for (const hal::Rect &rect : damageRects) {
            scissor(encoder, rect);
            [encoder setRenderPipelineState:device->clear];
            [encoder setDepthStencilState:device->depthReset];
            [encoder setFragmentBytes:clear length:sizeof(clear) atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            ++drawCalls;
            [encoder setFragmentBuffer:primitives offset:0 atIndex:0];
            if (opaqueCount != 0) {
                [encoder setRenderPipelineState:counting ? device->countingCore : device->opaque];
                [encoder setDepthStencilState:device->depthWrite];
                ++drawCalls;
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                            vertexStart:0
                            vertexCount:6
                          instanceCount:opaqueCount
                           baseInstance:0];
            }
        }
        std::uint32_t cursor = 0;
        std::size_t path = 0;
        std::size_t effect = 0;
        while (path < view.paths.size() || effect < view.effects.size()) {
            const bool pathNext =
                effect == view.effects.size() ||
                (path < view.paths.size() &&
                 view.paths[path].insertAfter <= view.effects[effect].insertAfter);
            const std::uint32_t insertAfter =
                pathNext ? view.paths[path].insertAfter : view.effects[effect].insertAfter;
            if (insertAfter > cursor) {
                if (encoder == nil) {
                    pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
                    pass.depthAttachment.loadAction = MTLLoadActionLoad;
                    pass.depthAttachment.storeAction = MTLStoreActionStore;
                    encoder = beginPass(commands, pass, uniforms);
                }
                drawRange(encoder, cursor, insertAfter);
                cursor = insertAfter;
            }
            if (encoder != nil) {
                [encoder endEncoding];
                encoder = nil;
            }
            if (pathNext) {
                paths.encodeFine(*device, commands, path++, view, damageRects, targets);
            } else {
                effects.backdrop(*device, commands, color, view.effects[effect++], extent);
            }
        }
        if (cursor < primitiveCount) {
            if (encoder == nil) {
                pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
                pass.depthAttachment.loadAction = MTLLoadActionLoad;
                pass.depthAttachment.storeAction = MTLStoreActionDontCare;
                encoder = beginPass(commands, pass, uniforms);
            }
            drawRange(encoder, cursor, primitiveCount);
        }
        if (encoder != nil) {
            [encoder endEncoding];
        }
    }
    bloomActive = request.bloom != nullptr;
    if (bloomActive) {
        if (output == nil || output.width != extent.width || output.height != extent.height) {
            MTLTextureDescriptor *descriptor =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                                   width:extent.width
                                                                  height:extent.height
                                                               mipmapped:NO];
            descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
            descriptor.storageMode = MTLStorageModePrivate;
            output = [device->device newTextureWithDescriptor:descriptor];
        }
        effects.bloom(*device, commands, color, output, *request.bloom);
    }
}
}
