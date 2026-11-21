#include "widgets/GpuNoiseEnvelope.h"

#include <algorithm>
#include <cmath>

namespace prism::widgets {
bool GpuNoiseEnvelope::step(double dt, double samplesPerPixel, bool scrolling) noexcept {
    auto remaining = audio_.available();
    const bool received = remaining != 0;
    while (remaining > 0) {
        const auto count = audio_.pop(incoming_.data(), std::min(remaining, incoming_.size()));
        scope_.append(std::span(incoming_).first(count));
        remaining -= count;
    }
    if (received) {
        populated_ = scope_.build(samplesPerPixel);
    }
    if (scrolling && populated_) {
        scroll_ = std::fmod(scroll_ + 40 * dt, columns_);
    }
    return received || (scrolling && populated_);
}
}
