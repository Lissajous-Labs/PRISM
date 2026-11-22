#include "telemetry/UmaTelemetry.h"

#include <atomic>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <numbers>
#include <thread>
#include <vector>

namespace {
using namespace prism::telemetry;

double responseDb(const std::array<double, halfBandTaps> &taps, double frequency) {
    std::complex<double> sum = 0;
    for (std::size_t n = 0; n < taps.size(); ++n) {
        sum +=
            taps[n] * std::polar(1.0, -2 * std::numbers::pi * frequency * static_cast<double>(n));
    }
    return 20 * std::log10(std::max(std::abs(sum), 1e-300));
}

bool checkHalfBandResponse() {
    const auto taps = designHalfBand();
    double ripple = 0;
    double stopband = -400;
    for (int i = 0; i <= 1000; ++i) {
        ripple = std::max(ripple, std::abs(responseDb(taps, 0.0468 * i / 1000)));
        stopband = std::max(stopband, responseDb(taps, 0.4532 + (0.5 - 0.4532) * i / 1000));
    }
    std::cout << "half-band passband ripple " << ripple << " dB, stopband " << stopband << " dB\n";
    return ripple < 0.001 && stopband < -100;
}

std::vector<double> lastBand(const UmaLayout &layout, std::uint32_t band, std::uint64_t written,
                             std::size_t count) {
    std::vector<double> out(count);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = layout.bands[band][(written - count + i) & umaMask];
    }
    return out;
}

double toneAmplitude(const std::vector<double> &x, double frequency, double rate) {
    std::complex<double> sum = 0;
    for (std::size_t n = 0; n < x.size(); ++n) {
        sum += x[n] * std::polar(1.0, -2 * std::numbers::pi * frequency * n / rate);
    }
    return 2 * std::abs(sum) / static_cast<double>(x.size());
}

double rms(const std::vector<double> &x) {
    double sum = 0;
    for (const double v : x) {
        sum += v * v;
    }
    return std::sqrt(sum / static_cast<double>(x.size()));
}

bool render(UmaLayout &layout, UmaWriter &writer, double frequency, double amplitude,
            std::size_t frames) {
    std::vector<float> block(64);
    for (std::size_t offset = 0; offset < frames; offset += block.size()) {
        for (std::size_t n = 0; n < block.size(); ++n) {
            block[n] = static_cast<float>(
                amplitude * std::sin(2 * std::numbers::pi * frequency * (offset + n) / 48000));
        }
        writer.write(block, block);
    }
    UmaSnapshot snap;
    return snapshot(layout, snap) && snap.frames == frames;
}

bool checkDecimatedBands() {
    auto layout = std::make_unique<UmaLayout>();
    UmaWriter writer(*layout, 48000);
    constexpr std::size_t frames = 96000;
    if (!render(*layout, writer, 60, 0.5, frames)) {
        return false;
    }

    UmaSnapshot snap;
    static_cast<void>(snapshot(*layout, snap));
    const std::array<double, umaBands> rates{6000, 12000, 24000, 48000};
    bool ok = true;
    for (std::uint32_t b = 0; b < umaBands; ++b) {
        const std::uint64_t expected = frames >> (umaBands - 1 - b);
        const auto x =
            lastBand(*layout, b, snap.bandFrames[b], static_cast<std::size_t>(rates[b]) / 4);
        const double amplitude = toneAmplitude(x, 60, rates[b]);
        std::cout << "band " << b << " frames " << snap.bandFrames[b] << " amplitude " << amplitude
                  << "\n";
        ok = ok && snap.bandFrames[b] == expected && std::abs(amplitude - 0.5) < 1e-4;
    }
    return ok;
}

bool checkAliasRejection() {
    auto layout = std::make_unique<UmaLayout>();
    UmaWriter writer(*layout, 48000);
    constexpr std::size_t frames = 48000;
    if (!render(*layout, writer, 23000, 0.5, frames)) {
        return false;
    }
    UmaSnapshot snap;
    static_cast<void>(snapshot(*layout, snap));
    const double band2 = rms(lastBand(*layout, 2, snap.bandFrames[2], 12000));
    const double rejection = 20 * std::log10(band2 / (0.5 / std::sqrt(2.0)));
    std::cout << "23 kHz alias in band 2: " << rejection << " dB\n";
    return rejection < -100;
}

bool checkConcurrentSnapshots() {
    auto layout = std::make_unique<UmaLayout>();
    UmaWriter writer(*layout, 48000);
    std::atomic<bool> done{false};
    std::atomic<std::uint64_t> torn{0};
    std::atomic<std::uint64_t> reads{0};
    std::thread reader([&] {
        std::uint64_t previous = 0;
        while (!done.load(std::memory_order_acquire)) {
            UmaSnapshot snap;
            if (!snapshot(*layout, snap)) {
                continue;
            }
            reads.fetch_add(1, std::memory_order_relaxed);
            const bool consistent = snap.frames == snap.sequence * 64 &&
                                    snap.bandFrames[0] == snap.frames / 8 &&
                                    snap.sequence >= previous;
            torn.fetch_add(consistent ? 0 : 1, std::memory_order_relaxed);
            previous = snap.sequence;
        }
    });

    std::vector<float> block(64, 0.25f);
    for (int i = 0; i < 200000; ++i) {
        writer.write(block, block);
    }
    done.store(true, std::memory_order_release);
    reader.join();
    std::cout << "concurrent snapshots " << reads.load() << ", torn " << torn.load() << "\n";
    return torn.load() == 0 && reads.load() > 0;
}
}

int main() {
    const bool ok = checkHalfBandResponse() && checkDecimatedBands() && checkAliasRejection() &&
                    checkConcurrentSnapshots();
    return ok ? 0 : 1;
}
