#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace prism::host {
enum class SurfaceState : std::uint8_t {
    detached,
    peerAttached,
    surfaceReady,
    suspended,
    deviceLostRecovering
};
enum class SurfaceEvent : std::uint8_t {
    peerValid,
    surfaceOk,
    occluded,
    revealed,
    gpuFault,
    rebuilt,
    detach
};

inline constexpr std::array<std::string_view, 5> surfaceStateNames{
    "detached", "peer-attached", "surface-ready", "suspended", "device-lost-recovering"};

struct LifecycleHook {
    void (*function)(void *) = nullptr;
    void *context = nullptr;

    void operator()() const {
        if (function != nullptr) {
            function(context);
        }
    }
};

[[nodiscard]] constexpr bool holdsSurface(SurfaceState state) noexcept {
    return state == SurfaceState::surfaceReady || state == SurfaceState::suspended ||
           state == SurfaceState::deviceLostRecovering;
}

[[nodiscard]] constexpr bool allowed(SurfaceState from, SurfaceEvent event,
                                     SurfaceState &to) noexcept {
    switch (event) {
    case SurfaceEvent::detach:
        to = SurfaceState::detached;
        return true;
    case SurfaceEvent::peerValid:
        to = SurfaceState::peerAttached;
        return from == SurfaceState::detached;
    case SurfaceEvent::surfaceOk:
        to = SurfaceState::surfaceReady;
        return from == SurfaceState::peerAttached;
    case SurfaceEvent::occluded:
        to = SurfaceState::suspended;
        return from == SurfaceState::surfaceReady;
    case SurfaceEvent::revealed:
        to = SurfaceState::surfaceReady;
        return from == SurfaceState::suspended;
    case SurfaceEvent::gpuFault:
        to = SurfaceState::deviceLostRecovering;
        return from == SurfaceState::surfaceReady;
    case SurfaceEvent::rebuilt:
        to = SurfaceState::surfaceReady;
        return from == SurfaceState::deviceLostRecovering;
    }
    return false;
}

class SurfaceLifecycle {
  public:
    void setDrain(LifecycleHook hook) noexcept { drain_ = hook; }
    void setRelease(LifecycleHook hook) noexcept { release_ = hook; }

    bool handle(SurfaceEvent event) {
        SurfaceState next = state_;
        if (!allowed(state_, event, next)) {
            ++rejected_;
            return false;
        }
        if (event == SurfaceEvent::detach && holdsSurface(state_)) {
            drain_();
            release_();
        }
        state_ = next;
        ++transitions_;
        return true;
    }

    [[nodiscard]] SurfaceState state() const noexcept { return state_; }
    [[nodiscard]] bool canRender() const noexcept { return state_ == SurfaceState::surfaceReady; }
    [[nodiscard]] std::uint64_t transitions() const noexcept { return transitions_; }
    [[nodiscard]] std::uint64_t rejected() const noexcept { return rejected_; }

  private:
    SurfaceState state_ = SurfaceState::detached;
    LifecycleHook drain_;
    LifecycleHook release_;
    std::uint64_t transitions_ = 0;
    std::uint64_t rejected_ = 0;
};
}
