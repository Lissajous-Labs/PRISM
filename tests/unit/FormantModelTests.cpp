#include "../support/AllocGuard.h"
#include "widgets/VowelFormants.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

int main() {
    using namespace prism;
    widgets::VowelFormants vowel;
    widgets::GpuFilterCurve curve;
    render::DrawList list;
    vowel.configure(0, 0, 0.5, 0);
    curve.build(vowel.response(), 20, 20000, -60, 24);
    curve.draw(list, {0, 0, 400, 200}, 2, 0xFFFFFFFFu);
    double maximum = 0;
    {
        test::AllocGuard guard;
        for (int frame = 0; frame < 10000; ++frame) {
            const double position = static_cast<double>(frame) / 9999;
            vowel.configure(position, std::sin(position * 6), 0.7, 0.2,
                            3 + static_cast<unsigned>(frame % 3));
            curve.build(vowel.response(), 20, 20000, -60, 24);
            list.clear();
            curve.draw(list, {0, 0, 400, 200}, 2, 0xFFFFFFFFu);
            render::FormantPayload payload;
            std::memcpy(&payload, list.payload().data() + list.primitives()[0].payloadOffset,
                        sizeof(payload));
            for (int sample = 0; sample <= 16; ++sample) {
                const double u = static_cast<double>(sample) / 16;
                const double frequency = 20 * std::pow(1000.0, u);
                const double expected =
                    200 *
                    (1 - std::clamp((vowel.response().magnitudeDb(frequency) + 60) / 84, 0.0, 1.0));
                const double actual = render::formantHeight(payload, 84, 400 * u);
                if (!std::isfinite(actual)) {
                    return 1;
                }
                maximum = std::max(maximum, std::abs(actual - expected));
            }
        }
        if (!guard.counts().empty()) {
            return 1;
        }
    }
    std::cout << "Animated formant height error " << maximum
              << " px; 10000 allocation-free updates\n";
    return maximum <= 0.02 ? 0 : 1;
}
