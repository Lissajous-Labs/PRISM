#pragma once

#include "math/Bitset64.h"
#include "math/SpringCable.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace prism::scene {
template <std::size_t Capacity> class AnimationClock {
  public:
    static constexpr double settleDistance = 1e-4;
    static constexpr double settleVelocity = 1e-3;

    void set(std::size_t id, double value) noexcept {
        springs_[id] = {{value, 0}, value, springs_[id].omega};
        active_.reset(id);
    }

    void animate(std::size_t id, double target, double omega) noexcept {
        Spring &spring = springs_[id];
        spring.target = target;
        spring.omega = omega;
        active_.set(id);
    }

    template <class Visitor> std::size_t step(double dt, Visitor &&visitor) noexcept {
        std::size_t visited = 0;
        active_.forEach([&](std::size_t id) {
            Spring &spring = springs_[id];
            spring.state = math::criticalSpringStep(spring.state, spring.target, spring.omega, dt);
            if (std::abs(spring.state.position - spring.target) < settleDistance &&
                std::abs(spring.state.velocity) < settleVelocity) {
                spring.state = {spring.target, 0};
                settled_[settledCount_++] = id;
            }
            visitor(id, spring.state.position);
            ++visited;
        });
        for (std::size_t i = 0; i < settledCount_; ++i) {
            active_.reset(settled_[i]);
        }
        settledCount_ = 0;
        return visited;
    }

    [[nodiscard]] bool running() const noexcept { return active_.any(); }
    [[nodiscard]] double value(std::size_t id) const noexcept {
        return springs_[id].state.position;
    }
    [[nodiscard]] double velocity(std::size_t id) const noexcept {
        return springs_[id].state.velocity;
    }

  private:
    struct Spring {
        math::SpringState state{0, 0};
        double target = 0;
        double omega = 1;
    };

    std::array<Spring, Capacity> springs_{};
    std::array<std::size_t, Capacity> settled_{};
    std::size_t settledCount_ = 0;
    math::Bitset64<Capacity> active_;
};
}
