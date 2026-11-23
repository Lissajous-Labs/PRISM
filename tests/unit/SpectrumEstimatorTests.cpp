#include "math/Pchip.h"
#include "telemetry/SpectrumEstimator.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

namespace {
using namespace prism;

bool checkWhiteNoiseBias() {
    telemetry::SpectrumSettings settings;
    settings.tiltDbPerOctave = 0;
    settings.knots = 96;
    telemetry::SpectrumEstimator estimator(settings);
    std::mt19937_64 random(1234);
    std::normal_distribution<double> noise(0, 0.1);
    std::vector<float> block(8192);
    std::vector<double> sum(settings.knots, 0.0);
    int frames = 0;

    for (int i = 0; i < 1400; ++i) {
        for (float &sample : block) {
            sample = static_cast<float>(noise(random));
        }
        estimator.push(block);
        if (!estimator.analyze(1.0 / 60)) {
            continue;
        }
        ++frames;
        for (std::size_t k = 0; k < sum.size(); ++k) {
            sum[k] += estimator.power()[k] * settings.sampleRate / 2;
        }
    }

    double worstOctave = 0;
    for (double low = 20; low < 20000; low *= 2) {
        double octave = 0;
        int count = 0;
        for (std::size_t k = 0; k < sum.size(); ++k) {
            const double f = estimator.frequencies()[k];
            if (f >= low && f < 2 * low) {
                octave += sum[k] / frames;
                ++count;
            }
        }
        if (count > 0) {
            worstOctave = std::max(worstOctave, std::abs(10 * std::log10(octave / count / 0.01)));
        }
    }
    double overall = 0;
    for (const double value : sum) {
        overall += value / frames;
    }
    const double overallBias = 10 * std::log10(overall / static_cast<double>(sum.size()) / 0.01);
    std::cout << "White noise: " << frames << " frames, overall bias " << overallBias
              << " dB, worst octave " << worstOctave << " dB\n";
    return std::abs(overallBias) < 0.05 && worstOctave < 0.15;
}

bool checkResolution() {
    telemetry::SpectrumEstimator estimator({});
    const double subBass = estimator.binWidth(0);
    const double high = estimator.binWidth(3);
    std::cout << "Sub-bass bin " << subBass << " Hz, high band bin " << high << " Hz, window "
              << 512.0 / 48 << " ms\n";
    return std::abs(subBass - 1.46484375) < 1e-9 && std::abs(high - 93.75) < 1e-9;
}

bool checkPchipShape() {
    std::mt19937_64 random(99);
    std::uniform_real_distribution<double> value(-90, 0);
    std::vector<double> x(64);
    std::vector<double> y(64);
    std::vector<double> m(64);
    double worst = 0;
    for (int trial = 0; trial < 200; ++trial) {
        double position = 0;
        for (std::size_t i = 0; i < x.size(); ++i) {
            position += 0.2 + (i % 3) * 0.3;
            x[i] = position;
            y[i] = trial % 2 == 0 ? value(random) : (i == 30 ? 0.0 : -90.0);
        }
        math::pchipSlopes(x, y, m);
        for (std::size_t i = 0; i + 1 < x.size(); ++i) {
            const double h = x[i + 1] - x[i];
            const double low = std::min(y[i], y[i + 1]);
            const double highest = std::max(y[i], y[i + 1]);
            for (int s = 0; s <= 64; ++s) {
                const double v = math::pchipEvaluate(y[i], y[i + 1], m[i], m[i + 1], h, s / 64.0);
                worst = std::max({worst, low - v, v - highest});
            }
        }
    }
    std::cout << "PCHIP worst excursion outside knot interval " << worst << " dB\n";
    return worst <= 1e-9;
}

bool checkPeakHold() {
    const auto run = [](double fps) {
        telemetry::SpectrumSettings settings;
        settings.knots = 8;
        telemetry::SpectrumEstimator estimator(settings);
        std::vector<float> loud(40000);
        for (std::size_t n = 0; n < loud.size(); ++n) {
            loud[n] =
                static_cast<float>(0.5 * std::sin(2 * 3.141592653589793 * 1000 * n / 48000.0));
        }
        estimator.push(loud);
        static_cast<void>(estimator.analyze(1 / fps));
        const double start = *std::max_element(estimator.peaks().begin(), estimator.peaks().end());
        estimator.push(std::vector<float>(40000, 0.0f));
        for (int frame = 0; frame < static_cast<int>(2 * fps); ++frame) {
            static_cast<void>(estimator.analyze(1 / fps));
        }
        const double end = *std::max_element(estimator.peaks().begin(), estimator.peaks().end());
        return start - end;
    };
    const double at60 = run(60);
    const double at120 = run(120);
    const double at240 = run(240);
    std::cout << "Peak hold drop after 2 s: " << at60 << " / " << at120 << " / " << at240
              << " dB\n";
    return std::abs(at60 - 9.6) < 1e-6 && std::abs(at60 - at120) < 1e-6 &&
           std::abs(at60 - at240) < 1e-6;
}

bool checkReassignment() {
    double worst = 0;
    for (const double hz : {41.3, 333.7, 1000.37, 7300.2}) {
        telemetry::SpectrumEstimator estimator({});
        std::vector<float> tone(48000);
        for (std::size_t n = 0; n < tone.size(); ++n) {
            tone[n] = static_cast<float>(0.5 * std::sin(2 * 3.141592653589793 * hz * n / 48000.0));
        }
        estimator.push(tone);
        const auto estimate = estimator.reassignedPeak(hz);
        if (!estimate) {
            return false;
        }
        const double bin = estimator.binWidth(hz < 125 ? 0 : hz < 500 ? 1 : hz < 2000 ? 2 : 3);
        std::cout << "Reassigned " << hz << " Hz -> " << *estimate << " Hz (bin " << bin
                  << " Hz)\n";
        worst = std::max(worst, std::abs(*estimate - hz) / bin);
    }
    std::cout << "Reassignment worst error " << worst << " bins\n";
    return worst < 1e-3;
}

bool checkConstantQ() {
    telemetry::SpectrumSettings settings;
    settings.constantQ = true;
    telemetry::SpectrumEstimator estimator(settings);
    const auto frequencies = estimator.constantQFrequencies();
    const double step = frequencies[1] / frequencies[0];
    double worstLevel = 0;
    double worstNeighbour = 0;
    double worstSecond = -300;
    for (const std::size_t k : {std::size_t{40}, std::size_t{100}, std::size_t{135},
                                std::size_t{170}, std::size_t{205}}) {
        const double hz = frequencies[k];
        telemetry::SpectrumEstimator tone(settings);
        std::vector<float> samples(96000);
        for (std::size_t n = 0; n < samples.size(); ++n) {
            samples[n] =
                static_cast<float>(0.5 * std::sin(2 * 3.141592653589793 * hz * n / 48000.0));
        }
        tone.push(samples);
        std::vector<double> power(frequencies.size());
        if (!tone.constantQ(power)) {
            return false;
        }
        const auto db = [&](std::size_t i) { return 10 * std::log10(power[i] / 0.125); };
        const double upper = 1 / std::exp2(1.0 / 24);
        const double hann =
            std::sin(3.141592653589793 * upper) / (3.141592653589793 * upper * (1 - upper * upper));
        worstLevel = std::max(worstLevel, std::abs(db(k)));
        worstNeighbour = std::max({worstNeighbour, std::abs(db(k - 1) - db(k) + 6.0206),
                                   std::abs(db(k + 1) - db(k) - 20 * std::log10(hann))});
        worstSecond = std::max({worstSecond, db(k - 2) - db(k), db(k + 2) - db(k)});
    }
    std::cout << "Constant-Q: " << frequencies.size() << " bins, ratio " << step
              << ", centre error " << worstLevel << " dB, neighbour deviation from Hann "
              << worstNeighbour << " dB, second neighbour " << worstSecond << " dB\n";
    return std::abs(step - std::exp2(1.0 / 24)) < 1e-12 && worstLevel < 0.05 &&
           worstNeighbour < 0.1 && worstSecond < -30;
}
}

int main() {
    const bool ok = checkResolution() && checkPchipShape() && checkPeakHold() &&
                    checkReassignment() && checkConstantQ() && checkWhiteNoiseBias();
    return ok ? 0 : 1;
}
