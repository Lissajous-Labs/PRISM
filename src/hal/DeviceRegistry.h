#pragma once

#include "core/IntrusivePtr.h"
#include "hal/HalTypes.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace prism::hal {
class SurfaceBackend;
struct SurfaceDesc;

class BackendDevice {
  public:
    virtual ~BackendDevice() = default;
    [[nodiscard]] virtual BackendKind kind() const noexcept = 0;
    [[nodiscard]] virtual std::unique_ptr<SurfaceBackend>
    createSurface(const SurfaceDesc &desc) = 0;
};

class DeviceRegistry final : public RefCounted {
  public:
    using Factory = std::unique_ptr<BackendDevice> (*)();

    static IntrusivePtr<DeviceRegistry> acquire(BackendKind kind, Factory factory);

    [[nodiscard]] BackendDevice &device() const noexcept { return *device_; }
    [[nodiscard]] BackendKind kind() const noexcept { return device_->kind(); }

  private:
    friend class IntrusivePtr<DeviceRegistry>;
    explicit DeviceRegistry(std::unique_ptr<BackendDevice> device) : device_(std::move(device)) {}
    ~DeviceRegistry() = default;

    std::unique_ptr<BackendDevice> device_;
};
}
