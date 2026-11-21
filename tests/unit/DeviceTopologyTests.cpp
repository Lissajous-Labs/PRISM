#include "../support/AllocGuard.h"
#include "hal/DeviceRegistry.h"
#include "hal/StagingRing.h"
#include "hal/SurfaceHealth.h"

#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace {
using namespace prism::hal;

std::atomic<int> factoryCalls{0};

class CountingDevice final : public BackendDevice {
  public:
    [[nodiscard]] BackendKind kind() const noexcept override { return BackendKind::cpuReference; }
    [[nodiscard]] std::unique_ptr<SurfaceBackend> createSurface(const SurfaceDesc &) override {
        return {};
    }
};

std::unique_ptr<BackendDevice> makeDevice() {
    factoryCalls.fetch_add(1);
    return std::make_unique<CountingDevice>();
}

bool checkRegistry() {
    std::vector<std::thread> threads;
    std::array<DeviceRegistry *, 16> seen{};
    for (std::size_t i = 0; i < seen.size(); ++i) {
        threads.emplace_back([&, i] {
            seen[i] = DeviceRegistry::acquire(BackendKind::cpuReference, makeDevice).get();
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }
    const auto shared = DeviceRegistry::acquire(BackendKind::cpuReference, makeDevice);
    for (DeviceRegistry *registry : seen) {
        if (registry != shared.get()) {
            return false;
        }
    }
    return shared->kind() == BackendKind::cpuReference && shared->useCount() >= 2;
}

bool checkStagingRing() {
    FrameRing<3> ring(64 * 128);
    struct alignas(64) Record {
        std::array<std::uint32_t, 16> words;
    };
    std::array<std::size_t, 4> frames{};
    for (std::size_t frame = 0; frame < frames.size(); ++frame) {
        ring.beginFrame();
        frames[frame] = ring.frameIndex();
        prism::test::AllocGuard guard;
        for (std::uint32_t i = 0; i < 128; ++i) {
            Record record{};
            for (std::uint32_t w = 0; w < 16; ++w) {
                record.words[w] = i * 16 + w + static_cast<std::uint32_t>(frame);
            }
            if (!ring.streamRecord(&record)) {
                return false;
            }
        }
        Record overflow{};
        if (ring.streamRecord(&overflow) || !guard.counts().empty()) {
            return false;
        }
        ring.finishFrame();
        const auto *data = reinterpret_cast<const std::uint32_t *>(ring.frameData());
        if (reinterpret_cast<std::uintptr_t>(data) % 64 != 0 ||
            data[127 * 16 + 15] != 127 * 16 + 15 + frame) {
            return false;
        }
    }
    return frames[0] == 0 && frames[1] == 1 && frames[2] == 2 && frames[3] == 0;
}

bool checkHealthIsolation() {
    std::array<SurfaceHealthMachine, 16> surfaces{};
    surfaces[3].reportFault();
    if (surfaces[3].state() != SurfaceHealth::recovering) {
        return false;
    }
    for (std::size_t i = 0; i < surfaces.size(); ++i) {
        if (i != 3 && surfaces[i].state() != SurfaceHealth::ready) {
            return false;
        }
    }
    return surfaces[3].completeRecovery() && !surfaces[3].completeRecovery() &&
           surfaces[3].recoveries() == 1 && surfaces[3].state() == SurfaceHealth::ready;
}
}

int main() {
    if (!checkRegistry() || factoryCalls.load() < 1 || !checkStagingRing() ||
        !checkHealthIsolation()) {
        std::cerr << "Device topology contract failed\n";
        return 1;
    }
    return 0;
}
