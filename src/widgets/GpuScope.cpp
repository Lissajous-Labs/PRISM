#include "widgets/GpuScope.h"

#include <algorithm>
#include <cmath>

namespace prism::widgets {
namespace {
float toUnit(float amplitude) noexcept { return std::clamp((amplitude + 1) / 2, 0.0f, 1.0f); }
}

void PhosphorBuffer::decay(double dt, double tau) noexcept {
    const auto factor = static_cast<float>(std::exp(-dt / tau));
    for (float &value : texels_) {
        value *= factor;
    }
}

void PhosphorBuffer::deposit(std::span<const float> strip, float intensity) noexcept {
    const std::size_t columns = strip.size() / 2;
    for (std::uint32_t x = 0; x < width_; ++x) {
        const std::size_t column =
            std::min(columns - 1, static_cast<std::size_t>(x) * columns / width_);
        const float low = strip[column * 2];
        const float high = strip[column * 2 + 1];
        for (std::uint32_t y = 0; y < height_; ++y) {
            const float level = 1 - (static_cast<float>(y) + 0.5f) / static_cast<float>(height_);
            const float half = 0.5f / static_cast<float>(height_);
            if (level + half >= low && level - half <= high) {
                float *texel = &texels_[(static_cast<std::size_t>(y) * width_ + x) * 4];
                for (int c = 0; c < 4; ++c) {
                    texel[c] += intensity;
                }
            }
        }
    }
}

GpuScope::GpuScope(std::uint32_t columns, std::size_t capacity)
    : columns_(columns), history_(capacity, 0.0f),
      strip_(static_cast<std::size_t>(columns) * 2, 0.5f),
      rmsStrip_(static_cast<std::size_t>(columns) * 2, 0.5f) {}

void GpuScope::append(std::span<const float> samples) noexcept {
    for (const float sample : samples) {
        history_[written_ % history_.size()] = sample;
        ++written_;
    }
}

float GpuScope::sampleAt(std::uint64_t absolute) const noexcept {
    const std::uint64_t oldest = written_ > history_.size() ? written_ - history_.size() : 0;
    const std::uint64_t clamped =
        std::clamp<std::uint64_t>(absolute, oldest, written_ == 0 ? 0 : written_ - 1);
    return history_[clamped % history_.size()];
}

float GpuScope::interpolate(double position) const noexcept {
    const auto base = static_cast<std::int64_t>(std::floor(position));
    const auto t = static_cast<float>(position - static_cast<double>(base));
    const auto at = [&](std::int64_t i) {
        return sampleAt(static_cast<std::uint64_t>(std::max<std::int64_t>(0, i)));
    };
    const float p0 = at(base - 1);
    const float p1 = at(base);
    const float p2 = at(base + 1);
    const float p3 = at(base + 2);
    return 0.5f * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t +
                   (3 * p1 - p0 - 3 * p2 + p3) * t * t * t);
}

bool GpuScope::build(double samplesPerPixel) noexcept {
    const auto window = static_cast<std::uint64_t>(std::ceil(samplesPerPixel * columns_));
    if (written_ < window + 2 || window + 2 > history_.size()) {
        return false;
    }
    const std::uint64_t oldest = written_ > history_.size() ? written_ - history_.size() : 0;
    const std::uint64_t latestStart = written_ - window - 1;
    trigger_.reset();
    bool armed = false;
    for (std::uint64_t n = oldest + 1; n <= latestStart; ++n) {
        const float sample = sampleAt(n);
        if (sample < level_ - hysteresis_) {
            armed = true;
        } else if (armed && sample >= level_ && sampleAt(n - 1) < level_) {
            trigger_ = n;
            armed = false;
        }
    }
    const std::uint64_t start = trigger_.value_or(latestStart);
    for (std::uint32_t x = 0; x < columns_; ++x) {
        float low;
        float high;
        double sumSquares = 0;
        std::size_t count = 0;
        if (samplesPerPixel >= 1) {
            const auto n0 = start + static_cast<std::uint64_t>(std::floor(x * samplesPerPixel));
            const auto n1 = std::max(
                n0 + 1, start + static_cast<std::uint64_t>(std::floor((x + 1) * samplesPerPixel)));
            low = high = sampleAt(n0 == 0 ? 0 : n0 - 1);
            for (std::uint64_t n = n0; n <= n1; ++n) {
                const float sample = sampleAt(n);
                low = std::min(low, sample);
                high = std::max(high, sample);
                sumSquares += static_cast<double>(sample) * sample;
                ++count;
            }
        } else {
            const double left = static_cast<double>(start) + x * samplesPerPixel;
            const double right = left + samplesPerPixel;
            const float a = interpolate(left);
            const float b = interpolate((left + right) / 2);
            const float c = interpolate(right);
            low = std::min({a, b, c});
            high = std::max({a, b, c});
            sumSquares = static_cast<double>(a) * a + static_cast<double>(b) * b +
                         static_cast<double>(c) * c;
            count = 3;
        }
        const float rms =
            count > 0 ? static_cast<float>(std::sqrt(sumSquares / static_cast<double>(count))) : 0;
        strip_[x * 2] = toUnit(low);
        strip_[x * 2 + 1] = toUnit(high);
        rmsStrip_[x * 2] = toUnit(-rms);
        rmsStrip_[x * 2 + 1] = toUnit(rms);
    }
    return true;
}
}
