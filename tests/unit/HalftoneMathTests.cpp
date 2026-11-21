#include "math/HalftoneLattice.h"
#include "math/ThermalPalette.h"
#include "math/WavetableWarp.h"

#include <array>
#include <cmath>
#include <iostream>
#include <numbers>

namespace {
bool latticeOracle() {
    using namespace prism::math;
    for (double scale : {0.25, 0.5, 1.0, 1.25, 1.5, 2.0, 4.0}) {
        const std::array lattices{HalftoneLattice::brick(8 * scale, 5 * scale, 0.5),
                                  HalftoneLattice::rotated(8 * scale, 0.63),
                                  HalftoneLattice::hexagonal(8 * scale)};
        for (const auto &lattice : lattices) {
            constexpr double fraction = 0.2;
            const double radius = circleRadiusForArea(fraction, lattice.area());
            std::size_t hits = 0;
            constexpr int n = 512;
            for (int j = 0; j < n; ++j) {
                for (int i = 0; i < n; ++i) {
                    const double u = (i + 0.5) / n, v = (j + 0.5) / n;
                    const Vector2 p{lattice.b1.x * u + lattice.b2.x * v,
                                    lattice.b1.y * u + lattice.b2.y * v};
                    const auto cell = lattice.cell(p);
                    if (std::hypot(cell.offset.x, cell.offset.y) <= radius) {
                        ++hits;
                    }
                    if (lattice.kind == HalftoneLatticeKind::hexagonal && i % 13 == 0 &&
                        j % 17 == 0) {
                        const double distance = std::hypot(cell.offset.x, cell.offset.y);
                        for (int y = -2; y <= 2; ++y) {
                            for (int x = -2; x <= 2; ++x) {
                                if (std::hypot(p.x - lattice.b1.x * x - lattice.b2.x * y,
                                               p.y - lattice.b1.y * x - lattice.b2.y * y) <
                                    distance - 1e-10) {
                                    std::cerr << "Hex nearest-cell oracle failed\n";
                                    return false;
                                }
                            }
                        }
                    }
                }
            }
            const double error = std::abs(static_cast<double>(hits) / (n * n) - fraction);
            if (error > 0.002) {
                std::cerr << "Area oracle error " << error << '\n';
                return false;
            }
        }
    }
    for (double width : {2.0, 3.0, 8.0}) {
        const double area = pillArea(width, 2, 0.8);
        if (std::abs(pillWidthForArea(area / 40, 40, 2, 0.8) - width) > 1e-12) {
            return false;
        }
    }
    const auto axes = superellipseAxesForArea(0.25, 64, 1, 2);
    if (std::abs(std::numbers::pi * axes.x * axes.y - 16) > 1e-12) {
        return false;
    }
    return true;
}

bool warpOracle() {
    using namespace prism::math;
    std::array<float, 64> data{};
    for (std::size_t i = 0; i < 32; ++i) {
        data[i] = static_cast<float>(std::sin(wavetableTurn * static_cast<double>(i) / 32));
        data[i + 32] = data[i] * 0.5f;
    }
    const WavetableView table{data, 2, 32};
    for (int i = -100; i < 100; ++i) {
        const double phase = i * 0.1;
        if (std::abs(table.sample(phase, 0.4) - 0.812 * std::sin(phase)) > 0.00015 ||
            std::abs(table.sample(phase, 0.4) - table.sample(phase + wavetableTurn, 0.4)) > 1e-12) {
            return false;
        }
    }
    for (int i = -8; i <= 8; ++i) {
        const double x = i / 8.0;
        if (std::abs(quantizedWarp(x, 8, 10) - x) > 1e-12) {
            return false;
        }
        const double left = (quantizedWarp(x, 8, 10) - quantizedWarp(x - 1e-7, 8, 10)) / 1e-7;
        const double right = (quantizedWarp(x + 1e-7, 8, 10) - quantizedWarp(x, 8, 10)) / 1e-7;
        if (std::abs(left - right) > 1e-6) {
            return false;
        }
    }
    const auto hot = thermalEmission(1);
    const auto cold = thermalEmission(0);
    if (hot.r <= 1 || hot.g <= 1 || cold.r >= 0.01 || thermalOklch(0).chroma != 0) {
        return false;
    }
    return true;
}
}
int main() {
    if (!latticeOracle() || !warpOracle()) {
        std::cerr << "Halftone math contract failed\n";
        return 1;
    }
    std::cout << "Lattice area, nearest-cell, palette and periodic warp oracles passed\n";
}
