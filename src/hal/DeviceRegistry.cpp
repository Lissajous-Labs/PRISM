#include "hal/DeviceRegistry.h"

namespace prism::hal {
namespace {
constexpr std::size_t backendCount = 2;
std::array<std::atomic<DeviceRegistry *>, backendCount> registries{};
}

IntrusivePtr<DeviceRegistry> DeviceRegistry::acquire(BackendKind kind, Factory factory) {
    auto &slot = registries[static_cast<std::size_t>(kind)];
    if (DeviceRegistry *existing = slot.load(std::memory_order_acquire)) {
        return IntrusivePtr<DeviceRegistry>(existing);
    }
    auto device = factory();
    if (!device) {
        return {};
    }
    auto *created = new DeviceRegistry(std::move(device));
    created->retain();
    DeviceRegistry *expected = nullptr;
    if (!slot.compare_exchange_strong(expected, created, std::memory_order_acq_rel)) {
        static_cast<void>(created->release());
        delete created;
        return IntrusivePtr<DeviceRegistry>(expected);
    }
    return IntrusivePtr<DeviceRegistry>(created);
}
}
