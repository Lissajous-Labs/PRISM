#include "backends/metal/MetalSurfaceState.h"
#include "backends/metal/MetalUpload.h"

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
}

void MetalSurfaceState::uploadFrame(const render::FrameView &view) {
    id<MTLDevice> gpu = device->device;
    upload(gpu, primitives, view.primitives.data(), view.primitives.size_bytes());
    upload(gpu, payload, view.payload.data(), view.payload.size_bytes());
    upload(gpu, transforms, view.transforms.data(), view.transforms.size_bytes());
    upload(gpu, clips, view.clips.data(), view.clips.size_bytes());
    batches.build(view.primitives, earlyDepth && view.effects.empty());
    indexScratch.assign(batches.opaqueIndices().begin(), batches.opaqueIndices().end());
    indexScratch.insert(indexScratch.end(), batches.ordered().begin(), batches.ordered().end());
    upload(gpu, indices, indexScratch.data(), indexScratch.size() * sizeof(std::uint32_t));
    const auto rows =
        static_cast<std::uint32_t>(view.gradientRows.size() / (render::gradientRowTexels * 4));
    gradients = rows ? uploadTexture(gpu, gradients, render::gradientRowTexels, rows, 4,
                                     view.gradientRows, texelScratch)
                     : nil;
    for (std::size_t slot = 0; slot < textureSlots; ++slot) {
        if (slot < view.textures.size()) {
            const auto &texture = view.textures[slot];
            textures[slot] = uploadTexture(gpu, textures[slot], texture.width, texture.height,
                                           texture.channels, texture.texels, texelScratch);
        } else {
            textures[slot] = nil;
        }
    }
}

void MetalSurfaceState::bindUber(id<MTLRenderCommandEncoder> encoder,
                                 const FrameUniforms &uniforms) {
    [encoder setVertexBuffer:primitives offset:0 atIndex:0];
    [encoder setVertexBuffer:payload offset:0 atIndex:1];
    [encoder setVertexBuffer:transforms offset:0 atIndex:2];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
    [encoder setVertexBuffer:indices offset:0 atIndex:5];
    [encoder setFragmentBuffer:primitives offset:0 atIndex:0];
    [encoder setFragmentBuffer:payload offset:0 atIndex:1];
    [encoder setFragmentBuffer:transforms offset:0 atIndex:2];
    [encoder setFragmentBuffer:clips offset:0 atIndex:3];
    [encoder setFragmentBuffer:counter offset:0 atIndex:6];
    [encoder setFragmentTexture:(gradients ? gradients : device->emptyTexture) atIndex:0];
    for (std::size_t slot = 0; slot < textureSlots; ++slot) {
        [encoder setFragmentTexture:(textures[slot] ? textures[slot] : device->emptyTexture)
                            atIndex:slot + 1];
    }
}

id<MTLRenderCommandEncoder> MetalSurfaceState::beginRect(id<MTLCommandBuffer> commands,
                                                         MTLRenderPassDescriptor *pass,
                                                         hal::Rect rect,
                                                         const FrameUniforms &uniforms) {
    id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
    [encoder setScissorRect:MTLScissorRect{static_cast<NSUInteger>(rect.x0),
                                           static_cast<NSUInteger>(rect.y0),
                                           static_cast<NSUInteger>(rect.x1 - rect.x0),
                                           static_cast<NSUInteger>(rect.y1 - rect.y0)}];
    bindUber(encoder, uniforms);
    return encoder;
}

void MetalSurfaceState::drawOrdered(id<MTLRenderCommandEncoder> encoder, std::uint32_t from,
                                    std::uint32_t to) {
    const auto opaqueCount = static_cast<NSUInteger>(batches.opaqueIndices().size());
    [encoder setDepthStencilState:device->depthTest];
    for (const render::DrawRun &run : batches.runs()) {
        const std::uint32_t start = std::max(run.first, from);
        const std::uint32_t end = std::min(run.first + run.count, to);
        if (end <= start) {
            continue;
        }
        id<MTLRenderPipelineState> pipeline =
            device->pipelines[static_cast<std::size_t>(run.pipeline)];
        if (counting && run.pipeline == render::UberPipeline::sourceOver) {
            pipeline = device->counting;
        }
        if (pipeline == nil) {
            continue;
        }
        [encoder setRenderPipelineState:pipeline];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:6
                  instanceCount:end - start
                   baseInstance:opaqueCount + start];
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
    const auto encodeRect = [&](hal::Rect clipped) {
        id<MTLRenderCommandEncoder> encoder = beginRect(commands, pass, clipped, uniforms);
        [encoder setRenderPipelineState:device->clear];
        [encoder setDepthStencilState:device->depthReset];
        [encoder setFragmentBytes:clear length:sizeof(clear) atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder setFragmentBuffer:primitives offset:0 atIndex:0];
        const auto opaqueCount = static_cast<NSUInteger>(batches.opaqueIndices().size());
        if (opaqueCount != 0) {
            [encoder setRenderPipelineState:counting ? device->countingCore : device->opaque];
            [encoder setDepthStencilState:device->depthWrite];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:6
                      instanceCount:opaqueCount
                       baseInstance:0];
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
            drawOrdered(encoder, cursor, insertAfter);
            cursor = std::max(cursor, insertAfter);
            [encoder endEncoding];
            if (pathNext) {
                paths.encodeFine(*device, commands, path++, view, clipped, targets);
            } else {
                effects.backdrop(*device, commands, color, view.effects[effect++], extent);
            }
            encoder = beginRect(commands, pass, clipped, uniforms);
        }
        drawOrdered(encoder, cursor, primitiveCount);
        [encoder endEncoding];
    };
    if (!view.effects.empty()) {
        encodeRect(viewport);
    } else {
        request.damage->forEachRect([&](hal::Rect rect) {
            const hal::Rect clipped{std::max(rect.x0, viewport.x0), std::max(rect.y0, viewport.y0),
                                    std::min(rect.x1, viewport.x1), std::min(rect.y1, viewport.y1)};
            if (!clipped.empty()) {
                encodeRect(clipped);
            }
        });
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
