#include "widgets/GpuSpectrum.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace prism::widgets {
namespace {
constexpr double lowFrequency = 20;
constexpr double highFrequency = 20000;
}

GpuSpectrum::GpuSpectrum(std::uint32_t columns, double sampleRate, float fallRate, float floorDb)
    : columns_(columns), sampleRate_(sampleRate), fallRate_(fallRate), floorDb_(floorDb),
      fft_(spectrumSize), window_(spectrumSize), input_(spectrumSize),
      spectrum_(spectrumSize / 2 + 1), levels_(columns, floorDb), instant_(columns, floorDb),
      strip_(static_cast<std::size_t>(columns) * 2, 0.0f),
      waterfall_(static_cast<std::size_t>(columns) * waterfallRows, 0.0f) {
    double sum = 0;
    for (std::size_t n = 0; n < spectrumSize; ++n) {
        window_[n] =
            0.5 - 0.5 * std::cos(2 * std::numbers::pi * static_cast<double>(n) / spectrumSize);
        sum += window_[n];
    }
    for (double &w : window_) {
        w *= 2 / sum;
    }
}

double GpuSpectrum::columnFrequency(std::uint32_t column) const noexcept {
    const double t = (static_cast<double>(column) + 0.5) / columns_;
    return lowFrequency * std::pow(highFrequency / lowFrequency, t);
}

void GpuSpectrum::analyze(std::span<const float> block, double dt) {
    for (std::size_t n = 0; n < spectrumSize; ++n) {
        input_[n] = (n < block.size() ? block[n] : 0.0f) * window_[n];
    }
    fft_.forward(input_, spectrum_);
    const double binWidth = sampleRate_ / spectrumSize;
    const auto magnitude = [&](std::size_t k) {
        return std::abs(spectrum_[std::min(k, spectrum_.size() - 1)]);
    };
    for (std::uint32_t column = 0; column < columns_; ++column) {
        const double f0 = lowFrequency * std::pow(highFrequency / lowFrequency,
                                                  static_cast<double>(column) / columns_);
        const double f1 = lowFrequency * std::pow(highFrequency / lowFrequency,
                                                  static_cast<double>(column + 1) / columns_);
        const auto k0 = static_cast<std::size_t>(std::ceil(f0 / binWidth));
        const auto k1 = static_cast<std::size_t>(std::floor(f1 / binWidth));
        double peak = 0;
        if (k1 >= k0) {
            for (std::size_t k = k0; k <= k1; ++k) {
                peak = std::max(peak, magnitude(k));
            }
        } else {
            const double position = columnFrequency(column) / binWidth;
            const auto below = static_cast<std::size_t>(std::floor(position));
            const double t = position - static_cast<double>(below);
            peak = magnitude(below) * (1 - t) + magnitude(below + 1) * t;
        }
        const float level =
            peak > 0 ? std::max(floorDb_, static_cast<float>(20 * std::log10(peak))) : floorDb_;
        instant_[column] = level;
        levels_[column] = std::max(level, levels_[column] - fallRate_ * static_cast<float>(dt));
    }
}

void GpuSpectrum::buildStrip(float minDb, float maxDb) {
    const auto unit = [&](float db) {
        return std::clamp((db - minDb) / (maxDb - minDb), 0.0f, 1.0f);
    };
    for (std::uint32_t column = 0; column < columns_; ++column) {
        const float here = unit(levels_[column]);
        const float left = column > 0 ? unit((levels_[column - 1] + levels_[column]) / 2) : here;
        const float right =
            column + 1 < columns_ ? unit((levels_[column + 1] + levels_[column]) / 2) : here;
        strip_[column * 2] = std::min({left, here, right});
        strip_[column * 2 + 1] = std::max({left, here, right});
    }
}

void GpuSpectrum::appendWaterfallRow(float minDb, float maxDb) {
    float *row = &waterfall_[static_cast<std::size_t>(waterfallOffset_) * columns_];
    for (std::uint32_t column = 0; column < columns_; ++column) {
        row[column] = std::clamp((levels_[column] - minDb) / (maxDb - minDb), 0.0f, 1.0f);
    }
    waterfallOffset_ = (waterfallOffset_ + 1) % waterfallRows;
}

void GpuSpectrum::draw(render::DrawList &list, render::RectF rect, std::uint32_t stripSlot,
                       std::uint32_t gradientRow, std::uint32_t lineColor,
                       std::uint32_t fillColor) const {
    list.setLinearGradient(gradientRow, {rect.x0, rect.y0}, {rect.x0, rect.y1});
    list.plotStrip(rect, stripSlot, columns_, 0, fillColor, 0, 0, true);
    list.clearGradient();
    list.plotStrip(rect, stripSlot, columns_, 1.5f, lineColor);
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
