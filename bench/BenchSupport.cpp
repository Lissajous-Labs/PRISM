#include "BenchSupport.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <iomanip>
#include <iostream>
#include <new>
#include <pthread/qos.h>

namespace {
std::atomic<bool> counting{false};
std::atomic<std::size_t> prismAllocations{0};
std::atomic<std::size_t> systemAllocations{0};

const void *imageOf(const void *address) noexcept {
    Dl_info info{};
    return dladdr(address, &info) != 0 ? info.dli_fbase : nullptr;
}

bool runtimeImage(const void *address) noexcept {
    Dl_info info{};
    return dladdr(address, &info) != 0 && info.dli_fname != nullptr &&
           (std::strstr(info.dli_fname, "libc++") != nullptr ||
            std::strstr(info.dli_fname, "libsystem_malloc") != nullptr);
}

struct CallerClass {
    const void *caller = nullptr;
    bool runtime = false;
    bool prism = false;
};

std::array<CallerClass, 256> callerClasses{};

bool prismCaller(const void *caller) noexcept {
    const auto slot = (reinterpret_cast<std::uintptr_t>(caller) >> 2) % callerClasses.size();
    CallerClass &known = callerClasses[slot];
    if (known.caller != caller) {
        known = {caller, runtimeImage(caller),
                 imageOf(caller) == imageOf(reinterpret_cast<const void *>(&prismCaller))};
    }
    if (!known.runtime) {
        return known.prism;
    }

    void *frames[32];
    const int count = backtrace(frames, 32);
    int index = 0;
    while (index < count && frames[index] != caller) {
        ++index;
    }
    while (index < count && runtimeImage(frames[index])) {
        ++index;
    }
    return index < count &&
           imageOf(frames[index]) == imageOf(reinterpret_cast<const void *>(&prismCaller));
}

[[gnu::noinline]] void *allocate(std::size_t size, std::size_t alignment, const void *caller) {
    if (counting.load(std::memory_order_relaxed)) {
        (prismCaller(caller) ? prismAllocations : systemAllocations)
            .fetch_add(1, std::memory_order_relaxed);
    }
    void *pointer =
        alignment > alignof(std::max_align_t)
            ? std::aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment)
            : std::malloc(size == 0 ? 1 : size);
    if (pointer == nullptr) {
        throw std::bad_alloc();
    }
    return pointer;
}
}

[[gnu::noinline]] void *operator new(std::size_t size) {
    return allocate(size, alignof(std::max_align_t), __builtin_return_address(0));
}
[[gnu::noinline]] void *operator new[](std::size_t size) {
    return allocate(size, alignof(std::max_align_t), __builtin_return_address(0));
}
[[gnu::noinline]] void *operator new(std::size_t size, std::align_val_t alignment) {
    return allocate(size, static_cast<std::size_t>(alignment), __builtin_return_address(0));
}
[[gnu::noinline]] void *operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate(size, static_cast<std::size_t>(alignment), __builtin_return_address(0));
}
void operator delete(void *pointer) noexcept { std::free(pointer); }
void operator delete[](void *pointer) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void *pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void *pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete(void *pointer, std::size_t, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void *pointer, std::size_t, std::align_val_t) noexcept {
    std::free(pointer);
}

namespace prism::bench {
std::optional<BenchContext> openContext(hal::Extent extent) {
    BenchContext context;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    context.device = backends::MetalDevice::create();
    if (!context.device) {
        return std::nullopt;
    }
    context.surface = context.metalDevice().createRenderSurface({extent});
    if (!context.surface) {
        return std::nullopt;
    }
    return context;
}

void countAllocations(bool enabled) noexcept {
    prismAllocations.store(0, std::memory_order_relaxed);
    systemAllocations.store(0, std::memory_order_relaxed);
    counting.store(enabled, std::memory_order_relaxed);
}

std::size_t allocationCount() noexcept { return prismAllocations.load(std::memory_order_relaxed); }

std::size_t systemAllocationCount() noexcept {
    return systemAllocations.load(std::memory_order_relaxed);
}

void Report::distribution(std::string_view name, telemetry::FrameStatistics &statistics) {
    const auto &moments = statistics.moments();
    std::cout << std::fixed << std::setprecision(4) << workload_ << ' ' << name << ": Q50 "
              << statistics.quantile(0.5) << " Q99 " << statistics.quantile(0.99) << " mean "
              << moments.mean << " sd " << std::sqrt(moments.variance()) << '\n';
}

void Report::value(std::string_view name, double value) {
    std::cout << std::fixed << std::setprecision(4) << workload_ << ' ' << name << ": " << value
              << '\n';
}

bool Report::below(std::string_view name, double value, double limit) {
    const bool ok = value < limit;
    std::cout << std::fixed << std::setprecision(4) << workload_ << " gate " << name << ' ' << value
              << " < " << limit << (ok ? " pass" : " FAIL") << '\n';
    pass_ = pass_ && ok;
    return ok;
}

bool Report::atLeast(std::string_view name, double value, double limit) {
    const bool ok = value >= limit;
    std::cout << std::fixed << std::setprecision(4) << workload_ << " gate " << name << ' ' << value
              << " >= " << limit << (ok ? " pass" : " FAIL") << '\n';
    pass_ = pass_ && ok;
    return ok;
}

int Report::finish() const { return pass_ ? 0 : 1; }
}
