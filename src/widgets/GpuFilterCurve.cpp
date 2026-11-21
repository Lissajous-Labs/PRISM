#include "widgets/GpuFilterCurve.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace prism::widgets {
telemetry::Biquad designBiquad(FilterShape shape, double sampleRate, double frequency, double q,
                               double gainDb) {
    const double w0 = 2 * std::numbers::pi * frequency / sampleRate;
    const double cosine = std::cos(w0);
    const double alpha = std::sin(w0) / (2 * q);
    const double a = std::pow(10.0, gainDb / 40);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (shape) {
    case FilterShape::lowpass:
        b0 = (1 - cosine) / 2, b1 = 1 - cosine, b2 = (1 - cosine) / 2;
        a0 = 1 + alpha, a1 = -2 * cosine, a2 = 1 - alpha;
        break;
    case FilterShape::highpass:
        b0 = (1 + cosine) / 2, b1 = -(1 + cosine), b2 = (1 + cosine) / 2;
        a0 = 1 + alpha, a1 = -2 * cosine, a2 = 1 - alpha;
        break;
    case FilterShape::bandpass:
        b0 = alpha, b1 = 0, b2 = -alpha;
        a0 = 1 + alpha, a1 = -2 * cosine, a2 = 1 - alpha;
        break;
    case FilterShape::peaking:
        b0 = 1 + alpha * a, b1 = -2 * cosine, b2 = 1 - alpha * a;
        a0 = 1 + alpha / a, a1 = -2 * cosine, a2 = 1 - alpha / a;
        break;
    case FilterShape::lowShelf: {
        const double root = 2 * std::sqrt(a) * alpha;
        b0 = a * ((a + 1) - (a - 1) * cosine + root);
        b1 = 2 * a * ((a - 1) - (a + 1) * cosine);
        b2 = a * ((a + 1) - (a - 1) * cosine - root);
        a0 = (a + 1) + (a - 1) * cosine + root;
        a1 = -2 * ((a - 1) + (a + 1) * cosine);
        a2 = (a + 1) + (a - 1) * cosine - root;
        break;
    }
    case FilterShape::highShelf: {
        const double root = 2 * std::sqrt(a) * alpha;
        b0 = a * ((a + 1) + (a - 1) * cosine + root);
        b1 = -2 * a * ((a - 1) + (a + 1) * cosine);
        b2 = a * ((a + 1) + (a - 1) * cosine - root);
        a0 = (a + 1) - (a - 1) * cosine + root;
        a1 = 2 * ((a - 1) - (a + 1) * cosine);
        a2 = (a + 1) - (a - 1) * cosine - root;
        break;
    }
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

bool FilterResponse::add(const telemetry::Biquad &section) {
    if (count_ >= maxSections) {
        return false;
    }
    sections_[count_++] = section;
    return true;
}

std::complex<double> FilterResponse::response(double frequency) const noexcept {
    const double w = 2 * std::numbers::pi * frequency / sampleRate_;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    std::complex<double> total = parallel_ ? 0 : 1;
    for (const telemetry::Biquad &s : sections()) {
        const auto value = (s.b0 + s.b1 * z1 + s.b2 * z2) / (1.0 + s.a1 * z1 + s.a2 * z2);
        total = parallel_ ? total + value : total * value;
    }
    return total;
}

double FilterResponse::magnitudeDb(double frequency) const noexcept {
    return 20 * std::log10(std::max(std::abs(response(frequency)), 1e-12));
}

void GpuFilterCurve::build(const FilterResponse &response, double lowFrequency,
                           double highFrequency, double minDb, double maxDb) {
    payload_.domain[0] = static_cast<float>(std::log(lowFrequency));
    payload_.domain[1] = static_cast<float>(std::log(highFrequency / lowFrequency));
    payload_.domain[2] = static_cast<float>(2 * std::numbers::pi / response.sampleRate());
    payload_.domain[3] = static_cast<float>(minDb);
    dbRange_ = static_cast<float>(maxDb - minDb);
    payload_.parallel = response.parallel() ? 1 : 0;
    payload_.sections = static_cast<std::uint32_t>(response.sections().size());
    for (std::size_t i = 0; i < response.sections().size(); ++i) {
        const auto &s = response.sections()[i];
        payload_.coefficients[i][0] = static_cast<float>(s.b0 + s.b1 + s.b2);
        payload_.coefficients[i][1] = static_cast<float>(s.b0 + s.b2);
        payload_.coefficients[i][2] = static_cast<float>(s.b0 - s.b2);
        payload_.coefficients[i][3] = static_cast<float>(1 + s.a1 + s.a2);
        payload_.coefficients[i][4] = static_cast<float>(1 - s.a2);
    }
}

void GpuFilterCurve::draw(render::DrawList &list, render::RectF rect, float thickness,
                          std::uint32_t color, const FilterCurveStyle &style) const {
    auto h = payload_;
    h.rect[0] = rect.x0;
    h.rect[1] = rect.y0;
    h.rect[2] = rect.x1;
    h.rect[3] = rect.y1;
    h.style[0] = thickness;
    h.style[1] = style.haloSigma;
    h.style[2] = style.haloGain;
    h.style[3] = style.fillAlpha;
    h.fillDecay = style.fillDecay;
    h.zeroLineAlpha = style.zeroLineAlpha;
    list.formantCurve(h, dbRange_, color);
}

}
