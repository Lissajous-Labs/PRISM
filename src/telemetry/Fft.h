#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <span>
#include <vector>

namespace prism::telemetry {
class Fft {
  public:
    explicit Fft(std::size_t size)
        : size_(size), twiddles_(size / 2), reversed_(size), work_(size) {
        std::size_t bits = 0;
        while ((std::size_t{1} << bits) < size) {
            ++bits;
        }
        for (std::size_t i = 0; i < size; ++i) {
            std::size_t r = 0;
            for (std::size_t b = 0; b < bits; ++b) {
                r |= ((i >> b) & 1) << (bits - 1 - b);
            }
            reversed_[i] = r;
        }
        for (std::size_t k = 0; k < size / 2; ++k) {
            const double angle =
                -2 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(size);
            twiddles_[k] = {std::cos(angle), std::sin(angle)};
        }
    }

    void forward(std::span<const double> input, std::span<std::complex<double>> output) {
        for (std::size_t i = 0; i < size_; ++i) {
            work_[reversed_[i]] = {input[i], 0};
        }
        for (std::size_t span = 2; span <= size_; span <<= 1) {
            const std::size_t half = span / 2;
            const std::size_t stride = size_ / span;
            for (std::size_t start = 0; start < size_; start += span) {
                for (std::size_t k = 0; k < half; ++k) {
                    const std::complex<double> t = twiddles_[k * stride] * work_[start + k + half];
                    work_[start + k + half] = work_[start + k] - t;
                    work_[start + k] += t;
                }
            }
        }
        for (std::size_t i = 0; i < output.size() && i < size_; ++i) {
            output[i] = work_[i];
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

  private:
    std::size_t size_;
    std::vector<std::complex<double>> twiddles_;
    std::vector<std::size_t> reversed_;
    std::vector<std::complex<double>> work_;
};
}
