#include "../support/ReferenceScene.h"
#include "backends/metal/MetalState.h"
#include "backends/metal/MetalUpload.h"
#include "math/ColorSpace.h"

#include <array>
#include <cmath>
#include <iostream>

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
