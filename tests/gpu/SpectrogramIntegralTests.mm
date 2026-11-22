#include "backends/metal/MetalSpectrogram.h"
#include "backends/metal/MetalState.h"
#include "backends/metal/MetalUpload.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

namespace {
bool check(const std::shared_ptr<prism::backends::MetalDeviceState> &device, std::uint32_t frames,
           std::uint32_t rows, double &worst) {
    using namespace prism;
    constexpr std::uint32_t period = 32;
    std::vector<float> samples(static_cast<std::size_t>(frames) * period);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        for (std::uint32_t n = 0; n < period; ++n) {
            const double phase = math::wavetableTurn * n / period;
            samples[static_cast<std::size_t>(frame) * period + n] = static_cast<float>(
                (0.1 + 0.3 * (frame % 19) / 18.0) * std::sin(phase) +
                0.2 * std::sin(3 * phase + frame * 0.017) + 0.08 * std::cos(7 * phase));
        }
    }
    backends::MetalSpectrogram native(device, {samples, frames, period}, rows);
    const NSUInteger fieldPitch = (frames * sizeof(std::uint16_t) + 255) & ~NSUInteger{255};
    const NSUInteger integralPitch = ((frames + 1) * sizeof(float) + 255) & ~NSUInteger{255};
    id<MTLBuffer> fieldBuffer = [device->device newBufferWithLength:fieldPitch * rows
                                                            options:MTLResourceStorageModeShared];
    id<MTLBuffer> integralBuffer =
        [device->device newBufferWithLength:integralPitch * (rows + 1)
                                    options:MTLResourceStorageModeShared];
    math::WavetableWarp warp;
    for (int update = 0; update < 2; ++update) {
        warp.bend = update * 0.1;
        if (!native.prepare(warp, 0.25f, update == 0 ? 0 : frames / 2, update == 0 ? frames : 1) ||
            !native.dispatch()) {
            return false;
        }
        id<MTLTexture> field = (__bridge id<MTLTexture>)native.field().nativeTexture;
        id<MTLTexture> integral = (__bridge id<MTLTexture>)native.integral().nativeTexture;
        id<MTLCommandBuffer> commands = [device->queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit copyFromTexture:field
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(frames, rows, 1)
                            toBuffer:fieldBuffer
                   destinationOffset:0
              destinationBytesPerRow:fieldPitch
            destinationBytesPerImage:fieldPitch * rows];
        [blit copyFromTexture:integral
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(frames + 1, rows + 1, 1)
                            toBuffer:integralBuffer
                   destinationOffset:0
              destinationBytesPerRow:integralPitch
            destinationBytesPerImage:integralPitch * (rows + 1)];
        [blit endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if (commands.status != MTLCommandBufferStatusCompleted) {
            return false;
        }

        const auto *fieldBytes = static_cast<const std::byte *>(fieldBuffer.contents);
        const auto *integralBytes = static_cast<const std::byte *>(integralBuffer.contents);
        std::vector<double> previous(frames + 1);
        for (std::uint32_t y = 0; y <= rows; ++y) {
            const auto *actual = reinterpret_cast<const float *>(integralBytes + y * integralPitch);
            const auto *values =
                y == 0 ? nullptr
                       : reinterpret_cast<const std::uint16_t *>(fieldBytes + (y - 1) * fieldPitch);
            double rowSum = 0;
            for (std::uint32_t x = 0; x <= frames; ++x) {
                if (y != 0 && x != 0) {
                    rowSum += backends::halfToFloat(values[x - 1]);
                    previous[x] += rowSum;
                }
                const double expected = previous[x];
                const double error = std::abs(actual[x] - expected) / std::max(1.0, expected);
                worst = std::max(worst, error);
                if (!std::isfinite(actual[x]) || ((x == 0 || y == 0) && actual[x] != 0) ||
                    error > 5e-7) {
                    std::cerr << "Spectral prefix mismatch " << frames << 'x' << rows << " at " << x
                              << ',' << y << ": " << actual[x] << " expected " << expected << '\n';
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
        if (!device) {
            return 77;
        }
        constexpr std::array<std::array<std::uint32_t, 2>, 9> dimensions{{{1, 1},
                                                                          {17, 17},
                                                                          {31, 33},
                                                                          {32, 32},
                                                                          {33, 129},
                                                                          {257, 256},
                                                                          {1023, 17},
                                                                          {1025, 129},
                                                                          {2049, 256}}};
        double worst = 0;
        for (const auto &size : dimensions) {
            if (!check(device, size[0], size[1], worst)) {
                return 1;
            }
        }
        std::cout << "Spectral integral worst normalized error " << worst << '\n';
    }
    return 0;
}
