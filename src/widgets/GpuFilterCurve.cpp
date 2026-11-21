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
    if (sections_.size() >= maxSections) {
        return false;
    }
    sections_.push_back(section);
    return true;
}

std::complex<double> FilterResponse::response(double frequency) const noexcept {
    const double w = 2 * std::numbers::pi * frequency / sampleRate_;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    std::complex<double> total = 1;
    for (const telemetry::Biquad &s : sections_) {
        total *= (s.b0 + s.b1 * z1 + s.b2 * z2) / (1.0 + s.a1 * z1 + s.a2 * z2);
    }
    return total;
}

double FilterResponse::magnitudeDb(double frequency) const noexcept {
    return 20 * std::log10(std::max(std::abs(response(frequency)), 1e-12));
}

void GpuFilterCurve::build(const FilterResponse &response, double lowFrequency,
                           double highFrequency, double minDb, double maxDb) {
    const auto frequency = [&](double t) {
        return lowFrequency * std::pow(highFrequency / lowFrequency, t);
    };
    const auto unit = [&](double db) {
        return static_cast<float>(std::clamp((db - minDb) / (maxDb - minDb), 0.0, 1.0));
    };
    double left = response.magnitudeDb(frequency(0));
    for (std::uint32_t x = 0; x < columns_; ++x) {
        const double center = response.magnitudeDb(frequency((x + 0.5) / columns_));
        const double right = response.magnitudeDb(frequency((x + 1.0) / columns_));
        strip_[x * 2] = unit(std::min({left, center, right}));
        strip_[x * 2 + 1] = unit(std::max({left, center, right}));
        left = right;
    }
}

}
