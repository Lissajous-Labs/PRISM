#pragma once

#include "Spring.h"
#include "Vector2.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace prism::math {
template <std::size_t Points>
    requires(Points >= 2)
class VerletCable {
  public:
    VerletCable(Vector2 start, Vector2 end, double restLength) noexcept
        : segmentLength_(restLength / static_cast<double>(Points - 1)) {
        for (std::size_t i = 0; i < Points; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(Points - 1);
            current_[i] = start + (end - start) * t;
        }
        previous_ = current_;
    }

    void setEnds(Vector2 start, Vector2 end) noexcept {
        current_.front() = start;
        current_.back() = end;
    }

    void step(double dt, Vector2 gravity, double damping, int iterations) noexcept {
        for (std::size_t i = 1; i + 1 < Points; ++i) {
            const Vector2 velocity = (current_[i] - previous_[i]) * damping;
            previous_[i] = current_[i];
            current_[i] = current_[i] + velocity + gravity * (dt * dt);
        }
        for (int iteration = 0; iteration < iterations; ++iteration) {
            for (std::size_t i = 0; i + 1 < Points; ++i) {
                relax(iteration % 2 == 0 ? i : Points - 2 - i);
            }
        }
    }

    [[nodiscard]] const std::array<Vector2, Points> &points() const noexcept { return current_; }
    [[nodiscard]] double segmentLength() const noexcept { return segmentLength_; }

  private:
    double segmentLength_;
    std::array<Vector2, Points> current_{};
    std::array<Vector2, Points> previous_{};

    void relax(std::size_t index) noexcept {
        const Vector2 delta = current_[index + 1] - current_[index];
        const double distance = length(delta);
        if (!(distance > segmentLength_)) {
            return;
        }
        const Vector2 correction = delta * ((distance - segmentLength_) / distance);
        const bool firstPinned = index == 0;
        const bool secondPinned = index + 2 == Points;
        if (firstPinned && secondPinned) {
            return;
        }
        if (firstPinned) {
            current_[index + 1] = current_[index + 1] - correction;
        } else if (secondPinned) {
            current_[index] = current_[index] + correction;
        } else {
            current_[index] = current_[index] + correction * 0.5;
            current_[index + 1] = current_[index + 1] - correction * 0.5;
        }
    }
};
}
