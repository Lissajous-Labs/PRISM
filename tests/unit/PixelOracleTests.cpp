#include "../support/AllocGuard.h"
#include "../support/PixelOracle.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
bool near(double actual, double expected, double tolerance = 1e-10) {
    return std::abs(actual - expected) <= tolerance;
}

bool check() {
    using namespace prism::test;
    const Pixel black[] = {{0, 0, 0, 1}};
    const Pixel red[] = {{1, 0, 0, 1}};
    const auto identical = comparePixels(black, black);
    const auto difference = comparePixels(black, red);
    if (identical.maximumAbsolute != 0 || !std::isinf(identical.psnr)
        || difference.maximumAbsolute != 1 || !near(difference.psnr, 6.020599913279624)) {
        return false;
    }
    const Pixel tiny[] = {{1e-200, 0, 0, 1}};
    const auto small = comparePixels(black, tiny);
    if (!near(small.psnr, 4006.0205999132796, 1e-9)) {
        return false;
    }
    struct Example { Lab first; Lab second; double distance; };
    constexpr Example examples[] = {
        {{50, 2.6772, -79.7751}, {50, 0, -82.7485}, 2.0425},
        {{50, 3.1571, -77.2803}, {50, 0, -82.7485}, 2.8615},
        {{50, 2.8361, -74.0200}, {50, 0, -82.7485}, 3.4412},
        {{50, -1.3802, -84.2814}, {50, 0, -82.7485}, 1.0000},
        {{50, -1.1848, -84.8006}, {50, 0, -82.7485}, 1.0000},
        {{50, -0.9009, -85.5211}, {50, 0, -82.7485}, 1.0000},
        {{50, 0, 0}, {50, -1, 2}, 2.3669},
        {{50, -1, 2}, {50, 0, 0}, 2.3669},
        {{50, 2.49, -0.001}, {50, -2.49, 0.0009}, 7.1792},
        {{50, 2.49, -0.001}, {50, -2.49, 0.0011}, 7.2195},
        {{0, 0, 0}, {100, 0, 0}, 100}
    };
    for (const auto& example : examples) {
        const auto distance = deltaE2000(example.first, example.second);
        if (!near(distance, example.distance, 5e-5)
            || !near(distance, deltaE2000(example.second, example.first))
            || deltaE2000(example.first, example.first) != 0) {
            return false;
        }
    }
    bool emptyRejected = false;
    bool sizeRejected = false;
    bool nanRejected = false;
    bool peakRejected = false;
    bool labRejected = false;
    try { static_cast<void>(comparePixels({}, {})); }
    catch (const std::invalid_argument&) { emptyRejected = true; }
    try { static_cast<void>(comparePixels(black, {})); }
    catch (const std::invalid_argument&) { sizeRejected = true; }
    const Pixel invalid[] = {{std::numeric_limits<double>::quiet_NaN(), 0, 0, 1}};
    try { static_cast<void>(comparePixels(invalid, black)); }
    catch (const std::invalid_argument&) { nanRejected = true; }
    try { static_cast<void>(comparePixels(black, black, 0)); }
    catch (const std::invalid_argument&) { peakRejected = true; }
    try { static_cast<void>(deltaE2000({0, 0, std::numeric_limits<double>::infinity()}, {0, 0, 0})); }
    catch (const std::invalid_argument&) { labRejected = true; }
    bool overflowRejected = false;
    const double largest = std::numeric_limits<double>::max();
    try { static_cast<void>(deltaE2000({50, largest, largest}, {50, -largest, -largest})); }
    catch (const std::overflow_error&) { overflowRejected = true; }
    AllocGuard guard;
    const auto guarded = comparePixels(black, red);
    const auto distance = deltaE2000(examples[0].first, examples[0].second);
    return emptyRejected && sizeRejected && nanRejected && peakRejected && labRejected
           && overflowRejected && guarded.maximumAbsolute == 1 && distance > 2 && guard.counts().empty();
}
}

int main() {
    if (!check()) {
        std::cerr << "Pixel oracle contract failed\n";
        return 1;
    }
    return 0;
}
