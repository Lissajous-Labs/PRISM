#include "../support/ReferenceScene.h"
#include "backends/metal/MetalState.h"
#include "backends/metal/MetalSurfaceState.h"
#include "backends/metal/MetalUpload.h"
#include "math/ColorSpace.h"

#include <array>
#include <cmath>
#include <iostream>

namespace {
bool checkDirectColor(const std::shared_ptr<prism::backends::MetalDeviceState> &created) {
    using namespace prism;
    auto &device = *created;
    constexpr std::array<std::array<std::uint8_t, 4>, 8> colors{{{255, 0, 0, 255},
                                                                 {0, 255, 0, 255},
                                                                 {0, 0, 255, 255},
                                                                 {255, 255, 255, 255},
                                                                 {255, 169, 35, 128},
                                                                 {88, 205, 251, 220},
                                                                 {19, 19, 19, 255},
                                                                 {245, 247, 217, 0}}};
    render::DrawList list;
    for (std::size_t i = 0; i < colors.size(); ++i) {
        const auto &c = colors[i];
        const float x = static_cast<float>(i * 16);
        list.roundedRect({x, 0, x + 16, 16}, {}, render::packColor(c[0], c[1], c[2], c[3]));
    }
    for (std::size_t i = 0; i < colors.size(); ++i) {
        const auto &c = colors[i];
        const double x = static_cast<double>(i * 16);
        render::VectorPath path;
        path.moveTo({x, 0});
        path.lineTo({x + 16, 0});
        path.lineTo({x + 16, 16});
        path.lineTo({x, 16});
        path.close();
        list.fillPath(path, render::packColor(c[0], c[1], c[2], c[3]));
    }
    backends::MetalSurfaceState state;
    state.device = created;
    state.extent = {128, 16};
    state.createTargets();
    state.uploadFrame(test::viewOf(list));
    id<MTLBuffer> zero = [device.device newBufferWithLength:256
                                                    options:MTLResourceStorageModeShared];
    std::array<std::uint32_t, colors.size()> indices{};
    for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = static_cast<std::uint32_t>(i);
    }
    backends::upload(device.device, state.indices, indices.data(), sizeof(indices));
    MTLTextureDescriptor *descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                                                           width:128
                                                          height:16
                                                       mipmapped:NO];
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> target = [device.device newTextureWithDescriptor:descriptor];
    const backends::FrameUniforms uniforms{{128, 16}, 16, 0, 1, 0, {0, 0}};
    const std::array pipelines{
        device.opaque, device.countingCore,
        device.pipelines[static_cast<std::size_t>(render::UberPipeline::sourceOver)],
        device.counting, device.pathComposite};
    for (const auto &pipeline : pipelines) {
        id<MTLCommandBuffer> commands = [device.queue commandBuffer];
        state.paths.prepare(device, commands, test::viewOf(list), state.extent, state.transforms,
                            state.clips);
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
        pass.depthAttachment.texture = state.depth;
        pass.depthAttachment.loadAction = MTLLoadActionClear;
        pass.depthAttachment.clearDepth = 1;
        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
        for (NSUInteger index = 0; index < 10; ++index) {
            [encoder setVertexBuffer:zero offset:0 atIndex:index];
            [encoder setFragmentBuffer:zero offset:0 atIndex:index];
        }
        [encoder setVertexBuffer:state.primitives offset:0 atIndex:0];
        [encoder setFragmentBuffer:state.primitives offset:0 atIndex:0];
        [encoder setVertexBuffer:state.transforms offset:0 atIndex:2];
        [encoder setFragmentBuffer:state.transforms offset:0 atIndex:2];
        [encoder setVertexBuffer:state.indices offset:0 atIndex:5];
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:4];
        [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:4];
        for (NSUInteger index = 0; index < 25; ++index) {
            [encoder setFragmentTexture:device.emptyTexture atIndex:index];
        }
        [encoder setRenderPipelineState:pipeline];
        [encoder setDepthStencilState:device.depthTest];
        if (pipeline == device.pathComposite) {
            for (std::size_t i = 0; i < colors.size(); ++i) {
                state.paths.composite(device, encoder, i);
            }
        } else {
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                        vertexStart:0
                        vertexCount:6
                      instanceCount:colors.size()];
        }
        [encoder endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if (commands.status != MTLCommandBufferStatusCompleted) {
            return false;
        }
        std::array<std::uint16_t, 128 * 16 * 4> actual{};
        [target getBytes:actual.data()
             bytesPerRow:128 * 8
              fromRegion:MTLRegionMake2D(0, 0, 128, 16)
             mipmapLevel:0];
        for (std::size_t i = 0; i < colors.size(); ++i) {
            const auto &c = colors[i];
            const double alpha = c[3] / 255.0;
            const auto expected = math::toDisplayP3({math::srgbDecode(c[0] / 255.0) * alpha,
                                                     math::srgbDecode(c[1] / 255.0) * alpha,
                                                     math::srgbDecode(c[2] / 255.0) * alpha});
            const std::array channels{expected.r, expected.g, expected.b, alpha};
            for (std::size_t channel = 0; channel < channels.size(); ++channel) {
                const auto value =
                    backends::halfToFloat(actual[(8 * 128 + i * 16 + 8) * 4 + channel]);
                if (std::abs(value - channels[channel]) > 0.001) {
                    std::cerr << "Direct color mismatch at patch " << i << " channel " << channel
                              << ": " << value << " expected " << channels[channel] << ", pipeline "
                              << (&pipeline - pipelines.data()) << '\n';
                    return false;
                }
            }
        }
    }
    return true;
}
}
int main() {
    @autoreleasepool {
        const auto device = prism::backends::createMetalDeviceState();
        if (not device) {
            std::cout << "No native GPU device available\n";
            return 77;
        }

        constexpr std::array<std::array<float, 4>, 8> pixels{{{0, 0, 0, 0},
                                                              {1, 1, 1, 1},
                                                              {1, 0, 0, 1},
                                                              {0, 1, 0, 1},
                                                              {0, 0, 1, 1},
                                                              {0.25f, 0.125f, 0.0625f, 0.5f},
                                                              {4, 2, 1, 1},
                                                              {-0.25f, 0.125f, 0.5f, 1}}};
        MTLTextureDescriptor *descriptor =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                               width:pixels.size()
                                                              height:1
                                                           mipmapped:NO];
        descriptor.storageMode = MTLStorageModeShared;
        descriptor.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> source = [device->device newTextureWithDescriptor:descriptor];
        [source replaceRegion:MTLRegionMake2D(0, 0, pixels.size(), 1)
                  mipmapLevel:0
                    withBytes:pixels.data()
                  bytesPerRow:sizeof(pixels)];
        descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
        descriptor.usage = MTLTextureUsageRenderTarget;
        id<MTLTexture> target = [device->device newTextureWithDescriptor:descriptor];
        id<MTLCommandQueue> queue = [device->device newCommandQueue];
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:device->presentDrawable];
        [encoder setFragmentTexture:source atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if (commands.status != MTLCommandBufferStatusCompleted) {
            return 1;
        }

        std::array<std::array<std::uint16_t, 4>, pixels.size()> actual{};
        [target getBytes:actual.data()
             bytesPerRow:sizeof(actual)
              fromRegion:MTLRegionMake2D(0, 0, pixels.size(), 1)
             mipmapLevel:0];
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            const auto &pixel = pixels[i];
            const auto p3 = prism::math::toDisplayP3({pixel[0], pixel[1], pixel[2]});
            const std::array expected{p3.r, p3.g, p3.b, static_cast<double>(pixel[3])};
            for (std::size_t channel = 0; channel < expected.size(); ++channel) {
                if (std::abs(prism::backends::halfToFloat(actual[i][channel]) - expected[channel]) >
                    0.002) {
                    std::cerr << "Linear Display P3 mismatch at pixel " << i << " channel "
                              << channel << '\n';
                    return 1;
                }
            }
        }
        if (not checkDirectColor(device)) {
            return 1;
        }
        std::cout << "Display P3 presentation preserves primaries, premultiplied alpha and HDR\n";
    }
    return 0;
}
