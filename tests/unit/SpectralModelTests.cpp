#include "../support/AllocGuard.h"
#include "widgets/GpuHalftoneSpectrogram.h"
#include "widgets/GpuSpectralMatrix.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>

int main() {
    using namespace prism;
    constexpr std::uint32_t frames = 8, period = 512, rows = 32;
    std::array<float, frames * period> samples{};
    for (std::uint32_t m = 0; m < frames; ++m) {
        for (std::uint32_t n = 0; n < period; ++n) {
            samples[m * period + n] = static_cast<float>(
                (0.2 + 0.3 * m / (frames - 1)) * std::sin(math::wavetableTurn * 17 * n / period));
        }
    }
    const math::WavetableView table{samples, frames, period};
    widgets::GpuHalftoneSpectrogram spectrogram(frames, period, rows);
    if (!spectrogram.build(table, {}, 0)) {
        return 1;
    }
    const auto field = spectrogram.field();
    for (std::uint32_t m = 0; m < frames; ++m) {
        double power = 0;
        for (std::uint32_t j = 0; j < rows; ++j) {
            const double k0 = j == 0 ? 0.5 : std::pow(255.0, static_cast<double>(j) / rows);
            const double k1 =
                j + 1 == rows ? 255.5 : std::pow(255.0, static_cast<double>(j + 1) / rows);
            const double value = field.texels[(rows - 1 - j) * frames + m];
            power += std::max(0.0, std::pow(10.0, (value * 66 - 66) / 10) - 2.51188643150958e-7) *
                     (k1 - k0 + 1);
        }
        const double amplitude = 0.2 + 0.3 * m / (frames - 1);
        if (std::abs(power - amplitude * amplitude) > 1e-5) {
            std::cerr << "Fractional harmonic power error " << power - amplitude * amplitude
                      << '\n';
            return 1;
        }
    }
    widgets::GpuSpectralMatrix matrix(32, 48000);
    std::array<float, 4096> block{};
    matrix.analyze(block, 1, 3.5);
    if (matrix.active()) {
        std::cerr << "Silence activates the matrix\n";
        return 1;
    }
    for (std::size_t n = 0; n < block.size(); ++n) {
        block[n] = static_cast<float>(0.8 * std::sin(math::wavetableTurn * 64 * n / block.size()));
    }
    matrix.analyze(block, 1, 0);
    const auto states = matrix.states();
    const auto peak = static_cast<std::uint32_t>(
        std::max_element(states.begin(), states.end(),
                         [](const auto &a, const auto &b) { return a.height < b.height; }) -
        states.begin());
    if (std::abs(std::log2(matrix.columnFrequency(peak) / 750)) > 0.4 ||
        states[peak].height < 0.7) {
        std::cerr << "Harmonic localization failed\n";
        return 1;
    }
    const auto before = states[peak].height;
    if (!matrix.push(block.data(), block.size())) {
        return 1;
    }
    matrix.consume(1.0 / 120);
    if (matrix.states()[peak].height < before - 0.1) {
        return 1;
    }
    render::DrawList list;
    spectrogram.draw(list, {0, 0, 320, 160}, 0, 0, 1, 0.4f, 3);
    matrix.draw(list, {320, 0, 640, 160}, 2, 0);
    {
        test::AllocGuard guard;
        for (int i = 0; i < 10000; ++i) {
            if (!matrix.push(block.data(), 128)) {
                return 1;
            }
            matrix.consume(1.0 / 240);
            if (i % 100 == 0 &&
                !spectrogram.build(table, {}, 0, static_cast<std::uint32_t>(i % frames), 1)) {
                return 1;
            }
            list.clear();
            spectrogram.draw(list, {0, 0, 320, 160}, 0, 0, 1, 0.5f, 3);
            matrix.draw(list, {320, 0, 640, 160}, 2, 0);
        }
    }
    matrix.stop();
    for (int i = 0; i < 1200; ++i) {
        matrix.consume(1.0 / 60);
    }
    if (matrix.active()) {
        std::cerr << "Stopped matrix never becomes idle\n";
        return 1;
    }
    std::cout << "Fractional spectral power, silence, telemetry, localization and 10,000 "
                 "allocation-free updates passed\n";
}
