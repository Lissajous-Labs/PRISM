#include "../support/AllocGuard.h"
#include "telemetry/LoudnessMeter.h"
#include "telemetry/TelemetryRing.h"
#include "widgets/GpuFilterCurve.h"
#include "widgets/GpuScope.h"
#include "widgets/GpuSpectrum.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
#include <thread>
#include <vector>

namespace {
using namespace prism;

bool checkRing() {
    auto ring = std::make_unique<telemetry::TelemetryRing<float, 4096>>();
    constexpr int total = 1 << 20;
    std::thread producer([&] {
        float block[64];
        for (int sent = 0; sent < total;) {
            for (int i = 0; i < 64; ++i) {
                block[i] = static_cast<float>(sent + i);
            }
            if (ring->push(block, 64)) {
                sent += 64;
            }
        }
    });
    std::vector<float> received;
    received.reserve(total);
    float buffer[256];
    while (received.size() < total) {
        const std::size_t count = ring->pop(buffer, 256);
        received.insert(received.end(), buffer, buffer + count);
    }
    producer.join();
    bool ordered = true;
    for (int i = 0; i < total; ++i) {
        ordered = ordered && received[static_cast<std::size_t>(i)] == static_cast<float>(i);
    }
    test::AllocGuard guard;
    const float sample = 1;
    for (int i = 0; i < 1000; ++i) {
        static_cast<void>(ring->push(&sample, 1));
        static_cast<void>(ring->pop(buffer, 1));
    }
    return ordered && guard.counts().empty();
}

std::vector<float> sine(double sampleRate, double frequency, double dbfs, double seconds,
                        double phase = 0) {
    std::vector<float> out(static_cast<std::size_t>(sampleRate * seconds));
    const double amplitude = std::pow(10.0, dbfs / 20);
    for (std::size_t n = 0; n < out.size(); ++n) {
        out[n] = static_cast<float>(
            amplitude * std::sin(2 * std::numbers::pi * frequency * n / sampleRate + phase));
    }
    return out;
}

telemetry::LoudnessReading measure(const std::vector<std::pair<double, double>> &segments,
                                   double sampleRate = 48000) {
    telemetry::LoudnessMeter meter(sampleRate);
    for (const auto &[dbfs, seconds] : segments) {
        const auto signal = sine(sampleRate, 1000, dbfs, seconds);
        meter.process(signal.data(), signal.data(), signal.size());
    }
    return meter.reading();
}

bool within(float value, float expected, float tolerance) {
    return std::abs(value - expected) <= tolerance;
}

bool checkLoudness() {
    const auto first = measure({{-23, 20}});
    const auto second = measure({{-33, 20}});
    const auto third = measure({{-36, 10}, {-23, 60}, {-36, 10}});
    const auto fourth = measure({{-72, 10}, {-36, 10}, {-23, 60}, {-36, 10}, {-72, 10}});
    const auto fifth = measure({{-26, 20}, {-20, 20.1}, {-26, 20}});
    const auto rate = measure({{-23, 20}}, 44100);
    telemetry::LoudnessMeter peakMeter(48000);
    const auto quarter = sine(48000, 12000, 0, 1, std::numbers::pi / 4);
    peakMeter.process(quarter.data(), quarter.data(), quarter.size());
    const auto peak = peakMeter.reading();
    telemetry::LoudnessMeter quiet(48000);
    std::vector<float> block(512, 0.0f);
    const bool realtime = [&] {
        test::AllocGuard inner;
        quiet.process(block.data(), block.data(), block.size());
        return inner.counts().empty();
    }();
    std::cout << "Loudness M " << first.momentary << " S " << first.shortTerm << " I "
              << first.integrated << "; -33: " << second.integrated << "; gated "
              << third.integrated << ", " << fourth.integrated << ", " << fifth.integrated
              << "; 44.1k " << rate.integrated << "; true peak " << peak.truePeak << " dBTP, RMS "
              << first.rmsLeft << '\n';
    return within(first.momentary, -23, 0.1f) && within(first.shortTerm, -23, 0.1f) &&
           within(first.integrated, -23, 0.1f) && within(second.integrated, -33, 0.1f) &&
           within(third.integrated, -23, 0.1f) && within(fourth.integrated, -23, 0.1f) &&
           within(fifth.integrated, -23, 0.1f) && within(rate.integrated, -23, 0.1f) &&
           within(peak.truePeak, 0, 0.2f) && within(first.rmsLeft, -26.01f, 0.05f) && realtime;
}

bool checkSpectrum() {
    widgets::GpuSpectrum spectrum(256, 48000, 0);
    const auto tone = sine(48000, 1000, -6.0206, 1.0);
    if (!spectrum.analyze(tone, 1.0 / 60)) {
        return false;
    }
    const auto &estimator = spectrum.estimator();
    std::uint32_t loudest = 0;
    double total = 0;
    for (std::uint32_t c = 0; c < 256; ++c) {
        loudest = spectrum.levels()[c] > spectrum.levels()[loudest] ? c : loudest;
        const double ratio = std::pow(20000.0 / 20.0, 1.0 / 255);
        const double width = estimator.frequencies()[c] * (std::sqrt(ratio) - 1 / std::sqrt(ratio));
        total += estimator.power()[c] * width;
    }
    const double totalDb = 10 * std::log10(total);
    const double expectedDb = 10 * std::log10(0.5 * 0.25);
    const double knotRatio = spectrum.columnFrequency(loudest) / 1000;

    const auto decayed = [&](double fps) {
        widgets::GpuSpectrum ballistic(64, 48000, 0);
        static_cast<void>(ballistic.analyze(sine(48000, 1000, 0, 1.0), 1 / fps));
        std::uint32_t peak = 0;
        for (std::uint32_t c = 0; c < 64; ++c) {
            peak = ballistic.levels()[c] > ballistic.levels()[peak] ? c : peak;
        }
        const double start = ballistic.levels()[peak];
        ballistic.push(std::vector<float>(48000, 0.0f));
        for (int frame = 0; frame < static_cast<int>(fps); ++frame) {
            static_cast<void>(ballistic.analyze({}, 1 / fps));
        }
        return start - ballistic.estimator().levels()[peak];
    };
    const double at60 = decayed(60);
    const double at120 = decayed(120);
    const double at144 = decayed(144);
    spectrum.buildStrip(-90, 0);
    spectrum.appendWaterfallRow(-90, 0);
    std::cout << "Spectrum tone power " << totalDb << " dB (expected " << expectedDb
              << "), loudest knot ratio " << knotRatio << "; release over 1 s: 60 fps " << at60
              << " dB, 120 fps " << at120 << ", 144 fps " << at144 << '\n';
    return std::abs(totalDb - expectedDb) < 0.1 && std::abs(knotRatio - 1) < 0.03 &&
           std::abs(at60 - at120) < 1e-6 && std::abs(at60 - at144) < 1e-6 && at60 > 20 &&
           spectrum.waterfallOffset() == 1;
}

bool checkScope() {
    widgets::GpuScope scope(200, 1 << 18);
    std::vector<float> signal(150000);
    for (std::size_t n = 0; n < signal.size(); ++n) {
        signal[n] = 0.5f * static_cast<float>(std::sin(2 * std::numbers::pi * n / 997.0));
        if (n % 4999 == 0) {
            signal[n] = 0.99f;
        }
    }
    scope.append(signal);
    if (!scope.build(500)) {
        return false;
    }
    std::size_t spikes = 0;
    for (std::uint32_t x = 0; x < 200; ++x) {
        spikes += scope.bands()[x * 2 + 1] > 0.99f ? 1 : 0;
    }
    bool connected = true;
    for (std::uint32_t x = 0; x + 1 < 200; ++x) {
        connected = connected && scope.bands()[x * 2] <= scope.bands()[(x + 1) * 2 + 1] &&
                    scope.bands()[(x + 1) * 2] <= scope.bands()[x * 2 + 1];
    }
    const auto trigger = scope.triggerSample();
    const bool triggered =
        trigger && scope.sampleAt(*trigger) >= 0 && scope.sampleAt(*trigger - 1) < 0;
    widgets::GpuScope zoomed(100, 4096);
    std::vector<float> ramp(64);
    for (std::size_t n = 0; n < ramp.size(); ++n) {
        ramp[n] = static_cast<float>(std::sin(n * 0.3));
    }
    zoomed.append(ramp);
    zoomed.setTrigger(10, 0);
    const bool smooth = zoomed.build(0.25) && zoomed.bands()[1] - zoomed.bands()[0] < 0.05f;
    widgets::PhosphorBuffer phosphor(32, 16);
    phosphor.deposit(scope.bands(), 1);
    const float initial = phosphor.at(16, 8) + phosphor.at(16, 4) + phosphor.at(16, 12);
    phosphor.decay(0.05, 0.1);
    phosphor.decay(0.05, 0.1);
    const float twoSteps = phosphor.at(16, 8) + phosphor.at(16, 4) + phosphor.at(16, 12);
    std::cout << "Scope spikes " << spikes << ", trigger " << trigger.value_or(0) << '\n';
    return spikes >= 20 && connected && triggered && smooth && initial > 0 &&
           within(twoSteps, initial * static_cast<float>(std::exp(-1.0)), 1e-5f);
}

bool checkFilterCurve() {
    widgets::FilterResponse response(48000);
    response.add(widgets::designBiquad(widgets::FilterShape::lowpass, 48000, 1000, std::sqrt(0.5)));
    const bool lowpass = within(static_cast<float>(response.magnitudeDb(1000)), -3.0103f, 1e-3f) &&
                         response.magnitudeDb(100) > -0.01;
    response.clear();
    response.add(widgets::designBiquad(widgets::FilterShape::peaking, 48000, 2500, 2, 9));
    const bool peaking = within(static_cast<float>(response.magnitudeDb(2500)), 9, 1e-3f);
    std::size_t sections = 0;
    widgets::FilterResponse cascade(48000);
    while (cascade.add(widgets::designBiquad(widgets::FilterShape::lowpass, 48000, 5000, 0.7))) {
        ++sections;
    }
    return lowpass && peaking && sections == 16;
}
}

int main() {
    if (!checkRing() || !checkLoudness() || !checkSpectrum() || !checkScope() ||
        !checkFilterCurve()) {
        std::cerr << "Telemetry contract failed\n";
        return 1;
    }
    return 0;
}
