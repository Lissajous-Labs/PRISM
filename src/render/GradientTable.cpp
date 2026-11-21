#include "render/GradientTable.h"

#include "math/ColorSpace.h"
#include "math/Hash64.h"
#include "render/FrameView.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace prism::render {
namespace {
struct StraightColor {
    math::LinearRgb rgb;
    double alpha;
};

StraightColor decode(std::uint32_t packed) noexcept {
    const auto channel = [&](int shift) {
        return static_cast<double>((packed >> shift) & 0xFFu) / 255;
    };
    return {
        {math::srgbDecode(channel(0)), math::srgbDecode(channel(8)), math::srgbDecode(channel(16))},
        channel(24)};
}

math::LinearRgb mix(const StraightColor &a, const StraightColor &b, double t,
                    GradientSpace space) noexcept {
    switch (space) {
    case GradientSpace::linearRgb:
        return {a.rgb.r + (b.rgb.r - a.rgb.r) * t, a.rgb.g + (b.rgb.g - a.rgb.g) * t,
                a.rgb.b + (b.rgb.b - a.rgb.b) * t};
    case GradientSpace::oklab:
        return math::fromOklab(math::mixOklab(math::toOklab(a.rgb), math::toOklab(b.rgb), t));
    case GradientSpace::oklch:
        return math::fromOklab(math::fromOklch(math::mixOklch(
            math::toOklch(math::toOklab(a.rgb)), math::toOklch(math::toOklab(b.rgb)), t)));
    }
    return a.rgb;
}

std::uint64_t keyOf(std::span<const GradientStop> stops, GradientSpace space) noexcept {
    std::array<std::uint8_t, 8> scratch{};
    std::uint64_t key =
        math::hash64(std::span<const std::uint8_t>(scratch), static_cast<std::uint64_t>(space));
    for (const GradientStop &stop : stops) {
        std::memcpy(scratch.data(), &stop.position, 4);
        std::memcpy(scratch.data() + 4, &stop.color, 4);
        key = math::hash64(std::span<const std::uint8_t>(scratch), key);
    }
    return key;
}
}

std::uint32_t GradientTable::add(std::span<const GradientStop> stops, GradientSpace space) {
    const std::uint64_t key = keyOf(stops, space);
    const auto found = std::find(keys_.begin(), keys_.end(), key);
    if (found != keys_.end()) {
        return static_cast<std::uint32_t>(found - keys_.begin());
    }
    keys_.push_back(key);
    bake(stops, space);
    return static_cast<std::uint32_t>(keys_.size() - 1);
}

std::uint32_t GradientTable::addHdr(std::span<const float, 1024> premultipliedRgba) {
    const auto bytes = std::as_bytes(premultipliedRgba);
    const auto key = math::hash64(std::span<const std::byte>(bytes));
    const auto found = std::find(keys_.begin(), keys_.end(), key);
    if (found != keys_.end()) {
        return static_cast<std::uint32_t>(found - keys_.begin());
    }
    keys_.push_back(key);
    rows_.insert(rows_.end(), premultipliedRgba.begin(), premultipliedRgba.end());
    return static_cast<std::uint32_t>(keys_.size() - 1);
}

void GradientTable::clear() noexcept {
    rows_.clear();
    keys_.clear();
}

void GradientTable::bake(std::span<const GradientStop> stops, GradientSpace space) {
    for (std::uint32_t texel = 0; texel < gradientRowTexels; ++texel) {
        const double t = static_cast<double>(texel) / (gradientRowTexels - 1);
        StraightColor result{{0, 0, 0}, 0};
        if (!stops.empty()) {
            std::size_t upper = 0;
            while (upper < stops.size() && stops[upper].position < t) {
                ++upper;
            }
            if (upper == 0) {
                result = decode(stops.front().color);
            } else if (upper == stops.size()) {
                result = decode(stops.back().color);
            } else {
                const GradientStop &low = stops[upper - 1];
                const GradientStop &high = stops[upper];
                const double span = high.position - low.position;
                const double local = span > 0 ? (t - low.position) / span : 1;
                const StraightColor a = decode(low.color);
                const StraightColor b = decode(high.color);
                result = {mix(a, b, local, space), a.alpha + (b.alpha - a.alpha) * local};
            }
        }
        rows_.insert(rows_.end(), {static_cast<float>(result.rgb.r * result.alpha),
                                   static_cast<float>(result.rgb.g * result.alpha),
                                   static_cast<float>(result.rgb.b * result.alpha),
                                   static_cast<float>(result.alpha)});
    }
}
}
