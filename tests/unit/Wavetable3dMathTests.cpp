#include "../support/AllocGuard.h"
#include "math/HalfFloat.h"
#include "widgets/GpuWavetable3d.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>

int main() {
    using namespace prism;
    for (std::uint32_t bits = 0; bits < 65536; ++bits) {
        if ((bits & 0x7C00u) == 0x7C00u && (bits & 0x3FFu) != 0) {
            continue;
        }
        const auto half = static_cast<std::uint16_t>(bits);
        if (math::floatToHalf(math::halfToFloat(half)) != half) {
            return 1;
        }
    }
    if (math::halfRound(1.00048828125f) != 1 || math::halfRound(1.00146484375f) != 1.001953125f) {
        return 1;
    }
    widgets::GpuWavetable3d widget;
    widgets::Wavetable3dSettings settings;
    settings.voices = 4;
    double maximum = 0;
    render::DrawList list;
    widget.draw(list, {0, 0, 320, 180}, 0, 0, settings);
    for (int i = 0; i < 10000; ++i) {
        settings.camera.yaw =
            -std::numbers::pi + 2 * std::numbers::pi * std::fmod(i * 0.61803398875, 1.0);
        settings.camera.pitch = -1.5 + 3 * std::fmod(i * 0.41421356237, 1.0);
        settings.camera.inverseDistance = 0.2 * std::fmod(i * 0.73205080757, 1.0);
        settings.position = static_cast<float>(std::fmod(i * 0.32471795724, 1.0));
        const auto &camera = settings.camera;
        long double ux = 0, uy = 0;
        const long double cy = std::cos(static_cast<long double>(camera.yaw)),
                          sy = std::sin(static_cast<long double>(camera.yaw));
        const long double cp = std::cos(static_cast<long double>(camera.pitch)),
                          sp = std::sin(static_cast<long double>(camera.pitch));
        for (int corner = 0; corner < 8; ++corner) {
            const long double x = corner & 1 ? 1 : -1,
                              y = (corner & 2 ? 1 : -1) *
                                  static_cast<long double>(camera.amplitudeScale),
                              z = corner & 4 ? 1 : -1;
            const long double xr = cy * x + sy * z, zr = -sy * x + cy * z;
            const long double yr = cp * y - sp * zr, depth = sp * y + cp * zr;
            const long double denominator = 1 - depth * camera.inverseDistance;
            const long double rx = xr / denominator, ry = yr / denominator;
            ux = std::max(ux, std::abs(rx));
            uy = std::max(uy, std::abs(ry));
            const auto p = camera.project(static_cast<double>(x),
                                          static_cast<double>(y / camera.amplitudeScale),
                                          static_cast<double>(z));
            maximum = std::max(
                maximum, static_cast<double>(std::max(std::abs(p.x - rx), std::abs(p.y - ry))) *
                             camera.fit(320, 180));
        }
        const long double fit = (1 - 2 * static_cast<long double>(camera.padding)) *
                                std::min(320 / (2 * ux), 180 / (2 * uy));
        maximum = std::max(maximum, std::abs(camera.fit(320, 180) - static_cast<double>(fit)));
        const auto u = widget.uniforms({0, 0, 320, 180}, settings);
        float previous = (u.flags & 1) != 0 ? 2 : -1;
        unsigned activeCount = 0, voiceCount = 0;
        for (std::uint32_t slice = 0; slice < u.slices + 1 + u.voices; ++slice) {
            bool active;
            int voice;
            const float position = render::wavetableSlice(u, slice, active, voice);
            if (((u.flags & 1) != 0 ? position > previous : position < previous) || position < 0 ||
                position > 1) {
                std::cerr << "Slice ordering failed at pose " << i << '\n';
                return 1;
            }
            previous = position;
            activeCount += active;
            voiceCount += voice >= 0;
            if (active && position != settings.position) {
                return 1;
            }
        }
        if (activeCount != 1 || voiceCount != settings.voices) {
            return 1;
        }
        {
            test::AllocGuard guard;
            list.clear();
            widget.draw(list, {0, 0, 320, 180}, 0, 0, settings);
        }
    }
    std::cout << "10,000 projection/slice poses; maximum double-reference error " << maximum
              << " px\n";
    return maximum <= 1e-5 ? 0 : 1;
}
