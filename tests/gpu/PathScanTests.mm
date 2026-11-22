#include "../support/Pcg.h"
#include "backends/metal/MetalState.h"

#include <array>
#include <cstring>
#include <iostream>
#include <vector>

int main() {
    @autoreleasepool {
        const auto device = prism::backends::createMetalDeviceState();
        if (not device) {
            std::cout << "No native GPU device available\n";
            return 77;
        }

        constexpr std::uint32_t partition = 256;
        constexpr std::uint32_t capacity = 1048583;
        const auto options = MTLResourceStorageModeShared;
        id<MTLBuffer> counts = [device->device newBufferWithLength:capacity * sizeof(std::uint32_t)
                                                           options:options];
        id<MTLBuffer> offsets =
            [device->device newBufferWithLength:(capacity + 1) * sizeof(std::uint32_t)
                                        options:options];
        id<MTLBuffer> state = [device->device
            newBufferWithLength:((capacity + partition - 1) / partition + 1) * sizeof(std::uint32_t)
                        options:options];
        id<MTLCommandQueue> queue = [device->device newCommandQueue];
        if (not counts or not offsets or not state or not queue) {
            return 1;
        }

        prism::test::Pcg random(761);
        constexpr std::array sizes{1u,   31u,  32u,   255u,  256u,  257u,    511u,
                                   512u, 513u, 8191u, 8192u, 8193u, capacity};
        for (const auto size : sizes) {
            for (int pattern = 0; pattern < 4; ++pattern) {
                auto *input = static_cast<std::uint32_t *>(counts.contents);
                std::vector<std::uint32_t> expected(size + 1, 0);
                for (std::uint32_t i = 0; i < size; ++i) {
                    input[i] = pattern == 0   ? 0u
                               : pattern == 1 ? 1u
                               : pattern == 2 ? random.next() % 128u
                                              : (i % partition == partition - 1 ? 65535u : 0u);
                    expected[i + 1] = expected[i] + input[i];
                }
                std::memset(offsets.contents, 0xCD, offsets.length);
                std::memset(state.contents, 0, state.length);
                std::array<std::uint32_t, 16> uniforms{};
                uniforms[3] = size;
                id<MTLCommandBuffer> commands = [queue commandBuffer];
                id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
                [encoder setComputePipelineState:device->pathScan];
                [encoder setBuffer:counts offset:0 atIndex:0];
                [encoder setBuffer:offsets offset:0 atIndex:1];
                [encoder setBytes:uniforms.data() length:sizeof(uniforms) atIndex:2];
                [encoder setBuffer:state offset:0 atIndex:3];
                [encoder dispatchThreadgroups:MTLSizeMake((size + partition - 1) / partition, 1, 1)
                        threadsPerThreadgroup:MTLSizeMake(partition, 1, 1)];
                [encoder endEncoding];
                [commands commit];
                [commands waitUntilCompleted];
                if (commands.status != MTLCommandBufferStatusCompleted) {
                    std::cerr << "GPU path scan failed\n";
                    return 1;
                }

                const auto *actual = static_cast<const std::uint32_t *>(offsets.contents);
                for (std::uint32_t i = 0; i <= size; ++i) {
                    if (actual[i] != expected[i]) {
                        std::cerr << "Scan mismatch: size=" << size << " pattern=" << pattern
                                  << " index=" << i << " actual=" << actual[i]
                                  << " expected=" << expected[i] << '\n';
                        return 1;
                    }
                }
            }
        }
        std::cout << "52 GPU prefix scans match the integer oracle, including 1,048,583 tiles\n";
    }
    return 0;
}
