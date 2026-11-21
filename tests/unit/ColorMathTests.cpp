#include "../support/AllocGuard.h"
#include "math/Color.h"
#include "math/ColorSpace.h"
#include "math/Dither.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>

namespace {
using namespace prism::math;

constexpr PremultipliedColor half(PremultipliedColor color) { return withCoverage(color, 0.5); }
static_assert(sourceOver(half({1, 0, 0, 1}), {0, 1, 0, 1}) == PremultipliedColor{0.5, 0.5, 0, 1});
static_assert(multiply({0.2, 0.2, 0.2, 0.4}, {0, 0, 0, 0}) ==
              PremultipliedColor{0.2, 0.2, 0.2, 0.4});

bool near(double actual, double expected, double tolerance) {
    return std::abs(actual - expected) <= tolerance;
}

double maxDifference(LinearRgb first, LinearRgb second) {
    return std::max(
        {std::abs(first.r - second.r), std::abs(first.g - second.g), std::abs(first.b - second.b)});
}

double unit(std::uint64_t &state) {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<double>(state >> 11) * 0x1p-53;
}

bool checkMatrices() {
    constexpr Matrix3 publishedLinear{{{4.0767416621, -3.3077115913, 0.2309699292},
                                       {-1.2684380046, 2.6097574011, -0.3413193965},
                                       {-0.0041960863, -0.7034186147, 1.7076147010}}};
    constexpr Matrix3 publishedLms{{{1.0, 0.3963377774, 0.2158037573},
                                    {1.0, -0.1055613458, -0.0638541728},
                                    {1.0, -0.0894841775, -1.2914855480}}};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            if (!near(linearFromLms[row][column], publishedLinear[row][column], 1e-8) ||
                !near(lmsFromOklab[row][column], publishedLms[row][column], 1e-7)) {
                return false;
            }
        }
    }
    const auto white = toOklab({1, 1, 1});
    const auto p3White = toDisplayP3({1, 1, 1});
    return near(white.lightness, 1, 1e-8) && std::hypot(white.a, white.b) < neutralChroma &&
           maxDifference(p3White, {1, 1, 1}) < 1e-12;
}

bool checkRoundTrips() {
    std::uint64_t state = 11;
    double transfer = 0;
    double wideGamut = 0;
    double perceptual = 0;
    for (int i = 0; i < 100000; ++i) {
        const LinearRgb color{2 * unit(state) - 0.5, 2 * unit(state) - 0.5, 2 * unit(state) - 0.5};
        const auto encoded = srgbEncode(color);
        transfer = std::max(transfer, maxDifference(srgbDecode(encoded), color));
        wideGamut = std::max(wideGamut, maxDifference(fromDisplayP3(toDisplayP3(color)), color));
        perceptual = std::max(perceptual,
                              maxDifference(fromOklab(fromOklch(toOklch(toOklab(color)))), color));
    }
    std::cout << "Round-trip errors: " << transfer << ' ' << wideGamut << ' ' << perceptual << '\n';
    return transfer < 1e-12 && wideGamut < 1e-12 && perceptual < 1e-6 &&
           near(srgbEncode(0.0031308), 12.92 * 0.0031308, 1e-15) &&
           near(srgbDecode(srgbEncode(0.0031308)), 0.0031308, 1e-15) &&
           near(srgbEncode(0.5), 0.735356983, 1e-9);
}

bool checkCompositing() {
    std::uint64_t state = 23;
    for (int i = 0; i < 10000; ++i) {
        const auto make = [&] {
            const double alpha = unit(state);
            return premultiply({unit(state), unit(state), unit(state), alpha});
        };
        const auto a = make();
        const auto b = make();
        const auto c = make();
        const auto left = sourceOver(sourceOver(a, b), c);
        const auto right = sourceOver(a, sourceOver(b, c));
        if (!near(left.r, right.r, 1e-12) || !near(left.a, right.a, 1e-12)) {
            return false;
        }
        const double coverage = unit(state);
        const auto covered = sourceOver(withCoverage(a, coverage), b);
        const auto blended = withCoverage(sourceOver(a, b), coverage);
        const auto expected = PremultipliedColor{blended.r + (1 - coverage) * b.r, 0, 0,
                                                 blended.a + (1 - coverage) * b.a};
        const auto screened = screen(a, b);
        const auto product = multiply(a, b);
        if (!near(covered.r, expected.r, 1e-12) || !near(covered.a, expected.a, 1e-12) ||
            screened.r < std::max(a.r, b.r) - 1e-12 ||
            !near(product.a, sourceOver(a, b).a, 1e-12) ||
            !near(multiply(a, {0, 0, 0, 0}).g, a.g, 1e-15) ||
            !near(unpremultiply(a).b * a.a, a.b, 1e-12)) {
            return false;
        }
    }
    const auto unpremultipliedClear = unpremultiply({0, 0, 0, 0});
    const auto mixed = sourceOver(half({1, 0, 0, 1}), {0, 1, 0, 1});
    return unpremultipliedClear.r == 0 && unpremultipliedClear.a == 0 &&
           near(srgbEncode(mixed.r), 0.735356983, 1e-9);
}

bool checkPerceptualMixing() {
    const Oklch red{0.6, 0.2, 0.1};
    const Oklch violet{0.4, 0.1, 2 * std::numbers::pi - 0.1};
    const Oklch gray{0.5, 0, 0};
    const auto wrapped = mixOklch(red, violet, 0.5);
    const auto fromGray = mixOklch(gray, red, 0.25);
    const auto toGray = mixOklch(red, gray, 0.75);
    const auto lab = mixOklab({0.2, -0.1, 0.3}, {0.8, 0.3, -0.1}, 0.25);
    return near(wrapped.hue, 0, 1e-12) && near(wrapped.chroma, 0.15, 1e-12) &&
           near(fromGray.hue, red.hue, 1e-12) && near(toGray.hue, red.hue, 1e-12) &&
           near(fromGray.chroma, 0.05, 1e-12) && near(lab.lightness, 0.35, 1e-12) &&
           near(lab.a, 0, 1e-12) && near(lab.b, 0.2, 1e-12) &&
           toOklch(toOklab({0.2, 0.2, 0.2})).hue == 0;
}

bool checkDither() {
    constexpr int samples = 1 << 16;
    double mean = 0;
    double variance = 0;
    for (int i = 0; i < samples; ++i) {
        const double noise = triangularNoise((i + 0.5) / samples);
        mean += noise;
        variance += noise * noise;
    }
    mean /= samples;
    variance = variance / samples - mean * mean;
    double worstMean = 0;
    double worstVariance = 0;
    for (int signal = 0; signal < 100; ++signal) {
        const double level = 100 + signal / 100.0;
        double errorSum = 0;
        double errorSquares = 0;
        for (int i = 0; i < samples; ++i) {
            const double error = quantizeDithered(level, (i + 0.5) / samples) - level;
            errorSum += error;
            errorSquares += error * error;
        }
        const double errorMean = errorSum / samples;
        worstMean = std::max(worstMean, std::abs(errorMean));
        worstVariance = std::max(worstVariance,
                                 std::abs(errorSquares / samples - errorMean * errorMean - 0.25));
    }
    std::cout << "Dither moments: " << mean << ' ' << variance << ' ' << worstMean << ' '
              << worstVariance << '\n';
    const double noise = interleavedGradientNoise(17, 29);
    return std::abs(mean) < 1e-3 && std::abs(variance - 1.0 / 6) < 1.0 / 600 && worstMean < 1e-3 &&
           worstVariance < 0.0025 && noise >= 0 && noise < 1 && quantizeLinear(-1, 0.5) == 0 &&
           quantizeLinear(2, 0.5) == 255;
}
}

int main() {
    const bool passed = checkMatrices() && checkRoundTrips() && checkCompositing() &&
                        checkPerceptualMixing() && checkDither();
    prism::test::AllocGuard guard;
    const auto guarded =
        toOklab(fromDisplayP3(toDisplayP3(srgbDecode(EncodedRgb{0.25, 0.5, 0.75}))));
    if (!passed || !guard.counts().empty() || !std::isfinite(guarded.lightness)) {
        std::cerr << "Color contract failed\n";
        return 1;
    }
    return 0;
}
