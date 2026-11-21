#include "widgets/VowelFormants.h"

#include "math/WavetableWarp.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace prism::widgets {
namespace {
using Table = std::array<std::array<double, 5>, 5>;
constexpr Table frequencyAnchors{{{800, 1150, 2900, 3900, 4950},
                                  {400, 1600, 2700, 3300, 4950},
                                  {350, 1700, 2700, 3700, 4950},
                                  {450, 800, 2830, 3800, 4950},
                                  {325, 700, 2530, 3500, 4950}}};
constexpr Table bandwidths{{{80, 90, 120, 130, 140},
                            {70, 80, 100, 120, 120},
                            {60, 90, 100, 120, 120},
                            {70, 80, 100, 130, 135},
                            {50, 60, 170, 180, 200}}};
constexpr Table gains{{{0, -6, -32, -20, -50},
                       {0, -14, -12, -14, -20},
                       {0, -15, -18, -20, -30},
                       {0, -11, -22, -22, -50},
                       {0, -16, -35, -40, -60}}};
double interpolate(const Table &table, double vowel, unsigned section) noexcept {
    const double position = std::clamp(vowel, 0.0, 1.0) * 4;
    const int base = static_cast<int>(std::floor(position));
    const auto at = [&](int i) {
        return table[static_cast<std::size_t>(std::clamp(i, 0, 4))][section];
    };
    return math::cubicSample(at(base - 1), at(base), at(base + 1), at(base + 2), position - base);
}
}
void VowelFormants::configure(double vowel, double shiftOctaves, double resonance, double drive,
                              unsigned sections) noexcept {
    response_.setParallel(true);
    response_.clear();
    const double shift = std::exp2(shiftOctaves);
    for (unsigned i = 0; i < std::clamp(sections, 3u, 5u); ++i) {
        const double frequency = std::clamp(interpolate(frequencyAnchors, vowel, i) * shift, 20.0,
                                            response_.sampleRate() * 0.45);
        frequencies_[i] = frequency;
        const double bandwidth = interpolate(bandwidths, vowel, i) * shift;
        const double q = frequency / bandwidth * (0.4 + 2 * std::clamp(resonance, 0.0, 1.0));
        auto section = designBiquad(FilterShape::bandpass, response_.sampleRate(), frequency, q);
        const double gain = std::pow(10.0, interpolate(gains, vowel, i) / 20) * (1 + 3 * drive);
        section.b0 *= gain;
        section.b1 *= gain;
        section.b2 *= gain;
        static_cast<void>(response_.add(section));
    }
}
void VowelFormants::configureFormantComb(double vowel, double shiftOctaves, double resonance,
                                         double drive) noexcept {
    response_.setParallel(false);
    response_.clear();
    const double sr = response_.sampleRate();
    const double shift = std::exp2(shiftOctaves);
    const double res = std::clamp(resonance, 0.0, 1.0);
    const double baseFrequency = (1500 + 560 * std::clamp(vowel, 0.0, 1.0)) * shift;
    static_cast<void>(response_.add(
        designBiquad(FilterShape::lowShelf, sr, std::clamp(260 * shift, 25.0, sr * 0.4), 0.42,
                     7.6 + 2 * drive)));
    static_cast<void>(response_.add(
        designBiquad(FilterShape::highShelf, sr, std::clamp(520 * shift, 40.0, sr * 0.4), 0.52,
                     -4.4)));
    static_cast<void>(response_.add(
        designBiquad(FilterShape::highShelf, sr, std::clamp(5800 * shift, 200.0, sr * 0.44), 0.60,
                     1.5)));
    constexpr std::array<double, 9> peakQ{4.6, 6.4, 8.2, 9.8, 11.2, 12.6, 13.8, 14.8, 15.6};
    constexpr std::array<double, 9> peakDb{10.6, 8.2, 6.8, 5.8, 5.0, 4.4, 3.8, 3.2, 2.6};
    for (std::size_t i = 0; i < peakQ.size(); ++i) {
        const double f = baseFrequency * static_cast<double>(i + 1);
        if (f >= sr * 0.46) {
            break;
        }
        if (i < frequencies_.size()) {
            frequencies_[i] = f;
        }
        const double q = peakQ[i] * (0.55 + 0.75 * res);
        const double gain = peakDb[i] * (0.45 + 0.92 * res);
        static_cast<void>(response_.add(designBiquad(FilterShape::peaking, sr, f, q, gain)));
    }
}
}
