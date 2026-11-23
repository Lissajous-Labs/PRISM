#include "backends/metal/MetalBackend.h"
#include "telemetry/SpectrumEstimator.h"
#include "telemetry/UmaTelemetry.h"

#import <Metal/Metal.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <vector>

int main() {
    @autoreleasepool {
        using namespace prism;
        auto device = backends::MetalDevice::create();
        if (!device) {
            return 77;
        }

        auto &metal = static_cast<backends::MetalDevice &>(*device);
        constexpr std::uint32_t columns = 300;
        constexpr std::uint32_t window = 3000;
        auto telemetry = metal.createTelemetry(columns);
        telemetry::UmaWriter writer(telemetry->layout(), 48000);

        std::vector<float> left(64);
        std::vector<float> right(64);
        std::uint64_t written = 0;
        for (int block = 0; block < 400; ++block) {
            for (std::size_t n = 0; n < left.size(); ++n) {
                const double t = static_cast<double>(written + n) / 48000;
                left[n] = static_cast<float>(0.7 * std::sin(2 * std::numbers::pi * 110 * t));
                right[n] = static_cast<float>(0.4 * std::sin(2 * std::numbers::pi * 1750 * t));
            }
            writer.write(left, right);
            written += left.size();
        }

        const backends::ScopeRequest request{2, window, 1.25f};
        if (!telemetry->prepareScope(request) || !telemetry->dispatch()) {
            std::cout << "telemetry scope dispatch failed\n";
            return 1;
        }

        const auto view = telemetry->scope();
        auto texture = (__bridge id<MTLTexture>)view.nativeTexture;
        id<MTLCommandQueue> queue = [texture.device newCommandQueue];
        id<MTLBuffer> readback = [texture.device newBufferWithLength:columns * 2 * sizeof(float)
                                                             options:MTLResourceStorageModeShared];
        auto commands = [queue commandBuffer];
        auto blit = [commands blitCommandEncoder];
        [blit copyFromTexture:texture
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(columns, 1, 1)
                            toBuffer:readback
                   destinationOffset:0
              destinationBytesPerRow:columns * 2 * sizeof(float)
            destinationBytesPerImage:columns * 2 * sizeof(float)];
        [blit endEncoding];
        [commands commit];
        [commands waitUntilCompleted];

        const auto &layout = telemetry->layout();
        const auto *gpu = static_cast<const float *>(readback.contents);
        const std::uint64_t end = telemetry->lastSnapshot().frames;
        float worst = 0;
        for (std::uint32_t column = 0; column < columns; ++column) {
            const std::uint32_t first = column * window / columns;
            const std::uint32_t last = std::max(first + 1, (column + 1) * window / columns);
            float low = INFINITY;
            float high = -INFINITY;
            for (std::uint32_t n = first; n < last; ++n) {
                const auto at = static_cast<std::uint32_t>((end - window + n) & telemetry::umaMask);
                const float value = 0.5f * (layout.scope[0][at] + layout.scope[1][at]) * 1.25f;
                low = std::min(low, value);
                high = std::max(high, value);
            }
            const float expectedLow = std::clamp((low + 1) * 0.5f, 0.0f, 1.0f);
            const float expectedHigh = std::clamp((high + 1) * 0.5f, 0.0f, 1.0f);
            worst = std::max({worst, std::abs(gpu[column * 2] - expectedLow),
                              std::abs(gpu[column * 2 + 1] - expectedHigh)});
        }

        std::cout << "telemetry scope frames " << end << " worst error " << worst << "\n";
        if (end != written || worst >= 1e-6f) {
            return 1;
        }

        telemetry::SpectrumSettings settings;
        settings.knots = 192;
        if (!telemetry->enableSpectrum(settings)) {
            std::cout << "spectrum pipelines unavailable\n";
            return 1;
        }
        telemetry::SpectrumEstimator reference(settings);
        std::vector<float> mono(64);
        std::uint32_t state = 12345;
        for (int block = 0; block < 800; ++block) {
            for (std::size_t n = 0; n < left.size(); ++n) {
                state = state * 1664525u + 1013904223u;
                const double noise = (static_cast<double>(state >> 8) / (1 << 24) - 0.5) * 0.2;
                const double t = static_cast<double>(written + n) / 48000;
                left[n] = static_cast<float>(noise + 0.3 * std::sin(2 * std::numbers::pi * 61 * t));
                right[n] =
                    static_cast<float>(noise + 0.2 * std::sin(2 * std::numbers::pi * 7300 * t));
                mono[n] = static_cast<float>(0.5 * (static_cast<double>(left[n]) + right[n]));
            }
            writer.write(left, right);
            reference.push(mono);
            written += left.size();
        }
        reference.push({});
        if (!reference.analyze(1.0 / 60) || !telemetry->prepareSpectrum() ||
            !telemetry->dispatch()) {
            std::cout << "spectrum dispatch failed\n";
            return 1;
        }
        const auto power = telemetry->spectrumPower();
        double worstRatio = 0;
        for (std::size_t k = 0; k < reference.power().size() && k < power.size(); ++k) {
            const double ratio = std::abs(10 * std::log10(power[k] / reference.power()[k]));
            worstRatio = std::max(worstRatio, ratio);
        }
        std::cout << "GPU spectrum knots " << power.size() << ", generation "
                  << telemetry->spectrumGeneration() << ", worst knot deviation from CPU "
                  << worstRatio << " dB\n";
        if (power.size() != reference.power().size() || !std::isfinite(worstRatio) ||
            worstRatio >= 0.01) {
            return 1;
        }

        using Clock = std::chrono::steady_clock;
        static_assert(Clock::is_steady);
        constexpr std::size_t warmup = 32;
        constexpr std::size_t samples = 512;
        std::array<double, samples> publicationReturnMs{};
        std::array<double, samples> publicationEntryMs{};
        for (std::size_t sample = 0; sample < warmup + samples; ++sample) {
            for (std::size_t n = 0; n < left.size(); ++n) {
                const double t = static_cast<double>(written + n) / settings.sampleRate;
                left[n] = static_cast<float>(0.3 * std::sin(2 * std::numbers::pi * 61 * t));
                right[n] = static_cast<float>(0.2 * std::sin(2 * std::numbers::pi * 7300 * t));
            }
            const auto previousGeneration = telemetry->spectrumGeneration();
            const auto publicationEntry = Clock::now();
            writer.write(left, right);
            const auto publicationReturn = Clock::now();
            const auto sequence = layout.publishedSeq.load(std::memory_order_acquire);
            if (!telemetry->prepareSpectrum() || !telemetry->dispatch()) {
                std::cout << "timed spectrum dispatch failed at sample " << sample << "\n";
                return 1;
            }
            const auto completionObserved = Clock::now();
            written += left.size();
            const auto &snapshot = telemetry->lastSnapshot();
            const auto completedPower = telemetry->spectrumPower();
            if (snapshot.sequence != sequence || snapshot.frames != written ||
                telemetry->spectrumGeneration() != previousGeneration + 1 ||
                completedPower.size() != settings.knots ||
                !std::all_of(completedPower.begin(), completedPower.end(),
                             [](float value) { return std::isfinite(value) && value > 0; })) {
                std::cout << "timed spectrum publication/completion mismatch at sample " << sample
                          << "\n";
                return 1;
            }
            if (sample >= warmup) {
                publicationReturnMs[sample - warmup] = std::chrono::duration<double, std::milli>(
                                                           completionObserved - publicationReturn)
                                                           .count();
                publicationEntryMs[sample - warmup] =
                    std::chrono::duration<double, std::milli>(completionObserved - publicationEntry)
                        .count();
            }
        }

        const auto report = [](const char *label, auto &durations) {
            std::sort(durations.begin(), durations.end());
            const auto quantile = [&](double q) {
                return durations[static_cast<std::size_t>(std::ceil(q * durations.size())) - 1];
            };
            double mean = 0;
            for (double duration : durations) {
                mean += duration / durations.size();
            }
            std::cout << label << " min " << durations.front() << ", Q50 " << quantile(0.50)
                      << ", Q95 " << quantile(0.95) << ", Q99 " << quantile(0.99) << ", max "
                      << durations.back() << ", mean " << mean << " ms\n";
        };
        std::cout << std::fixed << std::setprecision(6)
                  << "UMA audio publication -> host-observed GPU spectrum completion: " << samples
                  << " samples, " << warmup << " warmup, " << left.size() << " frames/block at "
                  << settings.sampleRate << " Hz, " << settings.knots << " knots\n";
        report("Publication-return endpoint", publicationReturnMs);
        report("Writer-entry endpoint", publicationEntryMs);
        std::cout
            << "The UMA release publication lies between writer entry and return; these endpoints "
               "bracket publication-to-host-observed completion. Completion is timed after "
               "synchronous Metal dispatch waits for its command buffer, including host wakeup. "
               "Serial synthetic audio producer, no frame pacing or concurrent producer; excludes "
               "audio capture/block accumulation, FFT window age, rendering, presentation and "
               "scanout. Display unavailable; audio-to-photon latency is not measured.\n";
        return 0;
    }
}
