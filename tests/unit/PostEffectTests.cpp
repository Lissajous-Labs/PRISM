#include "../support/Pcg.h"
#include "render/effects/Bloom.h"
#include "render/effects/DualKawase.h"
#include "render/effects/GaussianBlur.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>

namespace {
using namespace prism::render;

FloatImage randomImage(prism::test::Pcg &random, std::uint32_t w, std::uint32_t h) {
    FloatImage image;
    image.resize(w, h);
    for (auto &pixel : image.pixels) {
        for (float &channel : pixel) {
            channel = static_cast<float>(random.unit());
        }
    }
    return image;
}

double maxDifference(const FloatImage &a, const FloatImage &b) {
    double worst = 0;
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        for (std::size_t c = 0; c < 4; ++c) {
            worst = std::max(worst, static_cast<double>(std::abs(a.pixels[i][c] - b.pixels[i][c])));
        }
    }
    return worst;
}

bool checkWeights() {
    for (const double sigma : {0.5, 1.0, 2.5, 4.0, 9.0, 16.0}) {
        const auto weights = gaussianWeights(sigma);
        double total = weights[0];
        for (std::size_t i = 1; i < weights.size(); ++i) {
            total += 2 * weights[i];
        }
        const auto taps = pairedTaps(weights);
        if (std::abs(total - 1) > 1e-6 || taps.size() != (weights.size() + 2) / 2) {
            return false;
        }
    }
    return pairedTaps(gaussianWeights(10.0 / 3.0)).size() == 6;
}

bool checkPairedEqualsSeparable(prism::test::Pcg &random) {
    const FloatImage image = randomImage(random, 48, 40);
    double worst = 0;
    for (const double sigma : {0.8, 1.7, 3.0, 4.0}) {
        FloatImage separable;
        FloatImage paired;
        blurSeparable(image, separable, sigma);
        blurPaired(image, paired, sigma);
        worst = std::max(worst, maxDifference(separable, paired));
    }
    std::cout << "Paired versus separable blur: " << worst << '\n';
    return worst < 1e-5;
}

bool checkKawase() {
    FloatImage impulse;
    impulse.resize(128, 128);
    impulse.at(64, 64) = {1000, 1000, 1000, 1000};
    FloatImage blurred;
    kawaseBlur(impulse, blurred, 24);
    double total = 0;
    double spread = 0;
    for (std::uint32_t y = 0; y < 128; ++y) {
        for (std::uint32_t x = 0; x < 128; ++x) {
            const double value = blurred.at(x, y)[0];
            total += value;
            spread += value * ((x + 0.5 - 64) * (x + 0.5 - 64));
        }
    }
    const double deviation = std::sqrt(spread / total);
    const auto levels = kawaseLevels(24);
    std::cout << "Kawase energy " << total << ", deviation " << deviation << ", levels "
              << levels.levels << '\n';
    return std::abs(total - 1000) < 1 && deviation > 8 && deviation < 48 && levels.levels == 4;
}

bool checkBloomFireflies() {
    FloatImage scene;
    scene.resize(64, 64);
    for (auto &pixel : scene.pixels) {
        pixel = {0.05f, 0.05f, 0.05f, 1};
    }
    scene.at(31, 31) = {1000, 1000, 1000, 1};
    BloomSettings settings;
    FloatImage output;
    applyBloom(scene, output, settings);
    settings.karis = false;
    FloatImage naive;
    applyBloom(scene, naive, settings);
    const auto glowEnergy = [&](const FloatImage &image) {
        double energy = 0;
        for (std::size_t i = 0; i < image.pixels.size(); ++i) {
            energy += image.pixels[i][0] - scene.pixels[i][0];
        }
        return energy;
    };
    const double karis = glowEnergy(output);
    const double plain = glowEnergy(naive);
    const float lowMultiplier = softKneeMultiplier(0.1f, 1.0f, 0.5f);
    const float kneeContinuity =
        std::abs(softKneeMultiplier(1.5f, 1.0f, 0.5f) - softKneeMultiplier(1.5001f, 1.0f, 0.5f));
    std::cout << "Bloom firefly energy: Karis " << karis << ", plain " << plain << '\n';
    return karis < 2 && plain > 100 && lowMultiplier == 0 && kneeContinuity < 1e-3;
}
}

int main() {
    prism::test::Pcg random(101);
    if (!checkWeights() || !checkPairedEqualsSeparable(random) || !checkKawase() ||
        !checkBloomFireflies()) {
        std::cerr << "Post effect contract failed\n";
        return 1;
    }
    return 0;
}
