#include "BenchSupport.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <new>

namespace {
std::atomic<bool> counting{false};
std::atomic<std::size_t> allocations{0};

void *allocate(std::size_t size, std::size_t alignment) {
    if (counting.load(std::memory_order_relaxed)) {
        allocations.fetch_add(1, std::memory_order_relaxed);
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

void *operator new(std::size_t size) { return allocate(size, alignof(std::max_align_t)); }
void *operator new[](std::size_t size) { return allocate(size, alignof(std::max_align_t)); }
void *operator new(std::size_t size, std::align_val_t alignment) {
    return allocate(size, static_cast<std::size_t>(alignment));
}
void *operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate(size, static_cast<std::size_t>(alignment));
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
    allocations.store(0, std::memory_order_relaxed);
    counting.store(enabled, std::memory_order_relaxed);
}

std::size_t allocationCount() noexcept { return allocations.load(std::memory_order_relaxed); }

void Report::distribution(std::string_view name, telemetry::FrameStatistics &statistics) {
    const auto &moments = statistics.moments();
    std::cout << std::fixed << std::setprecision(4) << workload_ << ' ' << name << ": Q50 "
              << statistics.quantile(0.5) << " Q99 " << statistics.quantile(0.99) << " mean "
              << moments.mean << " sd " << std::sqrt(moments.variance()) << '\n';
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
