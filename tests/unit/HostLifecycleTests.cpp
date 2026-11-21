#include "../support/ReferenceScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "host/HostComponent.h"

#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace prism;
using host::SurfaceEvent;
using host::SurfaceState;

bool checkTransitionTable() {
    constexpr SurfaceState d = SurfaceState::detached, p = SurfaceState::peerAttached,
                           r = SurfaceState::surfaceReady, s = SurfaceState::suspended,
                           l = SurfaceState::deviceLostRecovering;
    struct Row {
        SurfaceState from;
        SurfaceEvent event;
        bool valid;
        SurfaceState to;
    };
    const std::vector<Row> table{
        {d, SurfaceEvent::peerValid, true, p},  {p, SurfaceEvent::surfaceOk, true, r},
        {r, SurfaceEvent::occluded, true, s},   {s, SurfaceEvent::revealed, true, r},
        {r, SurfaceEvent::gpuFault, true, l},   {l, SurfaceEvent::rebuilt, true, r},
        {d, SurfaceEvent::surfaceOk, false, d}, {p, SurfaceEvent::peerValid, false, p},
        {s, SurfaceEvent::gpuFault, false, s},  {l, SurfaceEvent::occluded, false, l},
        {r, SurfaceEvent::rebuilt, false, r},   {d, SurfaceEvent::revealed, false, d}};
    for (const Row &row : table) {
        SurfaceState next = row.from;
        if (host::allowed(row.from, row.event, next) != row.valid ||
            (row.valid && next != row.to)) {
            return false;
        }
    }
    for (const SurfaceState from : {d, p, r, s, l}) {
        SurfaceState next = from;
        if (!host::allowed(from, SurfaceEvent::detach, next) || next != d) {
            return false;
        }
    }
    return true;
}

bool checkDrainOrdering() {
    std::vector<std::string> events;
    host::SurfaceLifecycle lifecycle;
    lifecycle.setDrain(
        {[](void *log) { static_cast<std::vector<std::string> *>(log)->push_back("drain"); },
         &events});
    lifecycle.setRelease(
        {[](void *log) { static_cast<std::vector<std::string> *>(log)->push_back("release"); },
         &events});
    bool ok = true;
    for (int cycle = 0; cycle < 1000; ++cycle) {
        ok = ok && lifecycle.handle(SurfaceEvent::peerValid) &&
             lifecycle.handle(SurfaceEvent::surfaceOk) &&
             lifecycle.handle(SurfaceEvent::occluded) && !lifecycle.canRender() &&
             lifecycle.handle(SurfaceEvent::revealed) && lifecycle.canRender() &&
             !lifecycle.handle(SurfaceEvent::peerValid) && lifecycle.handle(SurfaceEvent::detach);
    }
    lifecycle.handle(SurfaceEvent::peerValid);
    lifecycle.handle(SurfaceEvent::detach);
    return ok && events.size() == 2000 && events.front() == "drain" && events[1] == "release" &&
           lifecycle.rejected() == 1000 && lifecycle.state() == SurfaceState::detached;
}

bool checkOffscreenComponent() {
    if (host::PassiveSurfaceHost::supported()) {
        return true;
    }
    backends::CpuRefDevice device;
    host::HostComponent component(device);
    render::DrawList list;
    test::buildReferenceScene(list, 0);
    const auto view = test::viewOf(list);
    bool ok = true;
    for (int cycle = 0; cycle < 20; ++cycle) {
        ok = ok && component.attachToPeer(nullptr, {0, 0, 128, 128}) &&
             component.renderFrame(view, nullptr, {0, 0, 0, 1});
        component.setOccluded(true);
        ok = ok && !component.renderFrame(view, nullptr, {0, 0, 0, 1});
        component.setOccluded(false);
        component.surface()->loseDevice();
        ok = ok && component.renderFrame(view, nullptr, {0, 0, 0, 1}) &&
             component.state() == SurfaceState::surfaceReady;
        component.detach();
        ok = ok && component.surface() == nullptr && component.state() == SurfaceState::detached;
    }
    return ok && component.drains() == 20;
}
}

int main() {
    if (!checkTransitionTable() || !checkDrainOrdering() || !checkOffscreenComponent()) {
        std::cerr << "Host lifecycle contract failed\n";
        return 1;
    }
    return 0;
}
