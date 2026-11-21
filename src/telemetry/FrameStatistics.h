#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace prism::telemetry {
struct Moments {
    std::size_t count = 0;
    double mean = 0;
    double m2 = 0;

    void add(double x) noexcept {
        ++count;
        const double delta = x - mean;
        mean += delta / static_cast<double>(count);
        m2 += delta * (x - mean);
    }
    [[nodiscard]] Moments merged(const Moments &other) const noexcept {
        if (count == 0) {
            return other;
        }
        if (other.count == 0) {
            return *this;
        }
        const auto total = count + other.count;
        const double delta = other.mean - mean;
        const double n = static_cast<double>(count), m = static_cast<double>(other.count);
        return {total, mean + delta * m / static_cast<double>(total),
                m2 + other.m2 + delta * delta * n * m / static_cast<double>(total)};
    }
    [[nodiscard]] double variance() const noexcept {
        return count > 1 ? m2 / static_cast<double>(count - 1) : 0;
    }
};

[[nodiscard]] inline double orderQuantile(std::span<double> sorted, double p) noexcept {
    if (sorted.empty()) {
        return 0;
    }
    const double h = static_cast<double>(sorted.size() - 1) * std::clamp(p, 0.0, 1.0);
    const auto low = static_cast<std::size_t>(std::floor(h));
    const auto high = static_cast<std::size_t>(std::ceil(h));
    return sorted[low] + (h - static_cast<double>(low)) * (sorted[high] - sorted[low]);
}

class FrameStatistics {
  public:
    explicit FrameStatistics(std::size_t window = 1024) : samples_(window), scratch_(window) {}

    void add(double value) noexcept {
        moments_.add(value);
        samples_[next_] = value;
        next_ = (next_ + 1) % samples_.size();
        filled_ = std::min(filled_ + 1, samples_.size());
    }
    [[nodiscard]] double quantile(double p) noexcept {
        std::copy_n(samples_.begin(), filled_, scratch_.begin());
        std::sort(scratch_.begin(), scratch_.begin() + static_cast<std::ptrdiff_t>(filled_));
        return orderQuantile(std::span(scratch_).first(filled_), p);
    }
    [[nodiscard]] const Moments &moments() const noexcept { return moments_; }
    [[nodiscard]] std::size_t size() const noexcept { return filled_; }
    [[nodiscard]] double recent(std::size_t age) const noexcept {
        return samples_[(next_ + samples_.size() - 1 - age) % samples_.size()];
    }

  private:
    std::vector<double> samples_;
    std::vector<double> scratch_;
    std::size_t next_ = 0;
    std::size_t filled_ = 0;
    Moments moments_;
};
}
