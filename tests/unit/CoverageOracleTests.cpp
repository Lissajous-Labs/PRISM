#include "../support/AllocGuard.h"
#include "../support/CoverageOracle.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
bool check() {
    using namespace prism::test;
    constexpr Point square[] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    constexpr Point hole[] = {{0.25, 0.25}, {0.25, 0.75}, {0.75, 0.75}, {0.75, 0.25}};
    constexpr Point sameDirection[] = {{0.25, 0.25}, {0.75, 0.25}, {0.75, 0.75}, {0.25, 0.75}};
    constexpr Point triangle[] = {{0, 0}, {1, 0}, {0, 1}};
    constexpr Point crossed[] = {{0, 0}, {1, 1}, {0, 1}, {1, 0}};
    constexpr Point degenerate[] = {{0.5, 0.5}, {0.5, 0.5}, {0.5, 0.5}};
    const Contour full[] = {square};
    const Contour cutout[] = {square, hole};
    const Contour nested[] = {square, sameDirection};
    const Contour duplicate[] = {square, square};
    const Contour diagonal[] = {triangle};
    const Contour bowtie[] = {crossed};
    const Contour emptyArea[] = {degenerate};
    if (stratifiedCoverage(full, {0, 0}) != 1 || stratifiedCoverage(full, {1, 0}) != 0
        || stratifiedCoverage(cutout, {0, 0}) != 0.75
        || stratifiedCoverage(nested, {0, 0}) != 1
        || stratifiedCoverage(nested, {0, 0}, FillRule::evenOdd) != 0.75
        || stratifiedCoverage(duplicate, {0, 0}) != 1
        || stratifiedCoverage(duplicate, {0, 0}, FillRule::evenOdd) != 0
        || stratifiedCoverage(emptyArea, {0, 0}) != 0
        || stratifiedCoverage({}, {0, 0}) != 0) {
        return false;
    }
    for (std::uint64_t seed = 0; seed < 32; ++seed) {
        for (const auto rule : {FillRule::nonZero, FillRule::evenOdd}) {
            if (std::abs(stratifiedCoverage(diagonal, {0, 0}, rule, seed) - 0.5) > 0.006
                || std::abs(stratifiedCoverage(bowtie, {0, 0}, rule, seed) - 0.5) > 0.006) {
                return false;
            }
        }
    }
    bool invalidRejected = false;
    bool originRejected = false;
    const Point invalid[] = {{std::numeric_limits<double>::quiet_NaN(), 0}, {1, 0}, {0, 1}};
    const Contour malformed[] = {invalid};
    try { static_cast<void>(stratifiedCoverage(malformed, {0, 0})); }
    catch (const std::invalid_argument&) { invalidRejected = true; }
    try { static_cast<void>(stratifiedCoverage(full, {1e100, 0})); }
    catch (const std::invalid_argument&) { originRejected = true; }
    const auto expected = stratifiedCoverage(bowtie, {0, 0}, FillRule::nonZero, 19);
    AllocGuard guard;
    const auto observed = stratifiedCoverage(bowtie, {0, 0}, FillRule::nonZero, 19);
    return invalidRejected && originRejected && expected == observed && guard.counts().empty();
}
}

int main() {
    if (!check()) {
        std::cerr << "Coverage oracle contract failed\n";
        return 1;
    }
    return 0;
}
