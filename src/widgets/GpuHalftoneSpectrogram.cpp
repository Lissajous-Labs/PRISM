#include "widgets/GpuHalftoneSpectrogram.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace prism::widgets {
namespace {
std::uint32_t checkedPeriod(std::uint32_t period) {
    if (period < 8 || !std::has_single_bit(period)) {
        throw std::invalid_argument(
            "Wavetable period must be a power of two of at least eight samples");
    }
    return period;
}
}
GpuHalftoneSpectrogram::GpuHalftoneSpectrogram(std::uint32_t frames, std::uint32_t period,
                                               std::uint32_t rows)
    : frames_(frames), period_(checkedPeriod(period)), rows_(rows), fft_(period_), input_(period_),
      spectrum_(period_ / 2 + 1), field_(static_cast<std::size_t>(frames) * rows),
      integral_(static_cast<std::size_t>(frames + 1) * (rows + 1) * 2), edges_(rows + 1),
      tiltWeights_(period_ / 2 + 1) {
    if (frames == 0 || rows == 0) {
        throw std::invalid_argument("Spectrogram dimensions must be nonzero");
    }
    const double maximum = std::min(512.0, period_ * 0.5 - 1);
    for (std::uint32_t j = 0; j <= rows_; ++j) {
        edges_[j] = j == 0       ? 0.5
                    : j == rows_ ? maximum + 0.5
                                 : std::pow(maximum, static_cast<double>(j) / rows_);
    }
}
bool GpuHalftoneSpectrogram::build(math::WavetableView table, const math::WavetableWarp &warp,
                                   double tilt, std::uint32_t firstFrame,
                                   std::uint32_t frameCount) {
    if (table.frames != frames_ || table.period != period_ ||
        table.samples.size() < static_cast<std::size_t>(frames_) * period_) {
        return false;
    }
    const auto end = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(frames_, static_cast<std::uint64_t>(firstFrame) + frameCount));
    const double maximum = std::min(512.0, period_ * 0.5 - 1);
    constexpr double floorPower = 2.51188643150958e-7;
    for (std::size_t k = 1; k < tiltWeights_.size(); ++k) {
        tiltWeights_[k] = std::pow(static_cast<double>(k), 2 * tilt);
    }
    for (std::uint32_t m = firstFrame; m < end; ++m) {
        const double morph = frames_ > 1 ? static_cast<double>(m) / (frames_ - 1) : 0;
        for (std::uint32_t n = 0; n < period_; ++n) {
            input_[n] = warp.sample(table, math::wavetableTurn * n / period_, morph);
        }
        fft_.forward(input_, spectrum_);
        for (std::uint32_t j = 0; j < rows_; ++j) {
            const double k0 = edges_[j], k1 = edges_[j + 1];
            double power = 0;
            const auto begin = static_cast<std::uint32_t>(std::max(1.0, std::ceil(k0 - 0.5)));
            const auto last = static_cast<std::uint32_t>(std::min(maximum, std::floor(k1 + 0.5)));
            for (std::uint32_t k = begin; k <= last; ++k) {
                const double overlap = std::max(0.0, std::min(k1, k + 0.5) - std::max(k0, k - 0.5));
                const double amplitudePower =
                    std::norm(spectrum_[k]) * 4 / (static_cast<double>(period_) * period_);
                power += overlap * amplitudePower * tiltWeights_[k];
            }
            power /= k1 - k0 + 1;
            const double normalized =
                std::clamp((10 * std::log10(power + floorPower) + 66) / 66, 0.0, 1.0);
            field_[static_cast<std::size_t>(rows_ - 1 - j) * frames_ + m] =
                static_cast<float>(normalized);
        }
    }
    std::fill(integral_.begin(), integral_.end(), 0);
    const std::size_t stride = static_cast<std::size_t>(frames_ + 1) * 2;
    for (std::uint32_t y = 0; y < rows_; ++y) {
        double row = 0;
        for (std::uint32_t x = 0; x < frames_; ++x) {
            row += field_[static_cast<std::size_t>(y) * frames_ + x];
            integral_[static_cast<std::size_t>(y + 1) * stride + (x + 1) * 2] = static_cast<float>(
                row + integral_[static_cast<std::size_t>(y) * stride + (x + 1) * 2]);
        }
    }
    return true;
}
void GpuHalftoneSpectrogram::draw(render::DrawList &list, render::RectF rect,
                                  std::uint32_t fieldSlot, std::uint32_t paletteRow,
                                  std::uint32_t noiseSlot, float position, unsigned unison,
                                  const HalftoneStyle &style) const {
    auto h = halftonePayload(rect, style, paletteRow, noiseSlot);
    h.modeFlags = render::halftoneFlags::summedArea | render::halftoneFlags::cursor |
                  (style.cleanBackdrop ? render::halftoneFlags::cleanBackdrop : 0u);
    h.cursorParams[0] = position;
    h.cursorParams[3] = static_cast<float>(std::clamp(unison, 1u, 16u));
    list.halftoneField(h, fieldSlot);
}
}
