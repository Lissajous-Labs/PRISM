#include "widgets/GpuSpectrum.h"

#include "math/Pchip.h"

#include <algorithm>
#include <cmath>

namespace prism::widgets {
namespace {
telemetry::SpectrumSettings settingsFor(std::uint32_t columns, double sampleRate, double tilt) {
    telemetry::SpectrumSettings settings;
    settings.sampleRate = sampleRate;
    settings.knots = std::max<std::uint32_t>(columns, 2);
    settings.tiltDbPerOctave = tilt;
    return settings;
}
}

GpuSpectrum::GpuSpectrum(std::uint32_t columns, double sampleRate, double tiltDbPerOctave)
    : columns_(std::max<std::uint32_t>(columns, 2)),
      estimator_(settingsFor(columns_, sampleRate, tiltDbPerOctave)),
      levels_(columns_, static_cast<float>(estimator_.settings().floorDb)),
      peaks_(columns_, static_cast<float>(estimator_.settings().floorDb)), units_(columns_, 0.0f),
      unitSlopes_(columns_, 0.0f), heights_(columns_, 0.0f), slopes_(columns_, 0.0f),
      strip_(static_cast<std::size_t>(columns_) * 2, 0.0f),
      waterfall_(static_cast<std::size_t>(columns_) * waterfallRows, 0.0f), positions_(columns_),
      unitValues_(columns_), unitDerivatives_(columns_) {
    for (std::uint32_t i = 0; i < columns_; ++i) {
        positions_[i] = static_cast<double>(i) / (columns_ - 1);
    }
}

bool GpuSpectrum::analyze(std::span<const float> block, double dt) noexcept {
    estimator_.push(block);
    if (!estimator_.analyze(dt)) {
        return false;
    }
    copyLevels();
    return true;
}

bool GpuSpectrum::apply(std::span<const float> power, double dt) noexcept {
    if (!estimator_.apply(power, dt)) {
        return false;
    }
    copyLevels();
    return true;
}

void GpuSpectrum::copyLevels() noexcept {
    const auto levels = estimator_.levels();
    const auto peaks = estimator_.peaks();
    for (std::uint32_t i = 0; i < columns_; ++i) {
        levels_[i] = static_cast<float>(levels[i]);
        peaks_[i] = static_cast<float>(peaks[i]);
    }
}

void GpuSpectrum::buildStrip(float minDb, float maxDb) {
    range_ = maxDb - minDb;
    const auto unit = [&](float db) { return std::clamp((db - minDb) / range_, 0.0f, 1.0f); };
    for (std::uint32_t i = 0; i < columns_; ++i) {
        units_[i] = unit(levels_[i]);
        unitValues_[i] = units_[i];
        const float left = i > 0 ? unit((levels_[i - 1] + levels_[i]) / 2) : units_[i];
        const float right = i + 1 < columns_ ? unit((levels_[i + 1] + levels_[i]) / 2) : units_[i];
        strip_[i * 2] = std::min({left, units_[i], right});
        strip_[i * 2 + 1] = std::max({left, units_[i], right});
    }
    math::pchipSlopes(positions_, unitValues_, unitDerivatives_);
    for (std::uint32_t i = 0; i < columns_; ++i) {
        unitSlopes_[i] = static_cast<float>(unitDerivatives_[i]);
    }
}

void GpuSpectrum::appendWaterfallRow(float minDb, float maxDb) {
    float *row = &waterfall_[static_cast<std::size_t>(waterfallOffset_) * columns_];
    for (std::uint32_t column = 0; column < columns_; ++column) {
        row[column] = std::clamp((levels_[column] - minDb) / (maxDb - minDb), 0.0f, 1.0f);
    }
    waterfallOffset_ = (waterfallOffset_ + 1) % waterfallRows;
}

void GpuSpectrum::draw(render::DrawList &list, render::RectF rect, std::uint32_t color,
                       float strokeWidth) {
    const float width = rect.x1 - rect.x0;
    const float height = rect.y1 - rect.y0;
    for (std::uint32_t i = 0; i < columns_; ++i) {
        heights_[i] = rect.y0 + (1 - units_[i]) * height;
        slopes_[i] = -unitSlopes_[i] * height / width;
    }
    list.spectrumCurve(rect, heights_, slopes_, strokeWidth, color);
}

void GpuSpectrum::drawWaterfall(render::DrawList &list, render::RectF rect, std::uint32_t slot,
                                std::uint32_t tint) const {
    const float split = static_cast<float>(waterfallOffset_) / waterfallRows;
    const float height = rect.y1 - rect.y0;
    const float newestHeight = split * height;
    list.texturedQuad(render::PrimitiveKind::texturedQuad,
                      {rect.x0, rect.y0, rect.x1, rect.y0 + newestHeight}, {0, 0, 1, split}, slot,
                      tint);
    list.texturedQuad(render::PrimitiveKind::texturedQuad,
                      {rect.x0, rect.y0 + newestHeight, rect.x1, rect.y1}, {0, split, 1, 1}, slot,
                      tint);
}
}
