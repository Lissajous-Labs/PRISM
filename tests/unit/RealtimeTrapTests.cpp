#include "core/Realtime.h"
#include "telemetry/TelemetryRing.h"

#include <memory>
#include <vector>

#pragma clang diagnostic ignored "-Wfunction-effects"

namespace {
std::vector<float> *sink = nullptr;

void allocateOnAudioThread(float value) noexcept PRISM_NONBLOCKING { sink->push_back(value); }
}

int main(int argc, char **) {
    auto ring = std::make_unique<prism::telemetry::TelemetryRing<float, 1024>>();
    std::vector<float> block(256, 0.5f);
    for (int i = 0; i < 1000; ++i) {
        static_cast<void>(ring->push(block.data(), block.size()));
        static_cast<void>(ring->pop(block.data(), block.size()));
    }
    if (argc > 1) {
        std::vector<float> storage;
        sink = &storage;
        for (int i = 0; i < 64; ++i) {
            allocateOnAudioThread(static_cast<float>(i));
        }
    }
    return 0;
}
