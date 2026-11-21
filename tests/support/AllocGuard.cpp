#include "AllocGuard.h"

#include <cstdlib>
#include <limits>
#include <new>

namespace prism::test::detail {
thread_local AllocationState allocationState;
}

namespace {
void* allocate(std::size_t size, std::size_t alignment = 0) {
    auto& state = prism::test::detail::allocationState;
    if (state.depth != 0) {
        ++state.counts.allocations;
        throw std::bad_alloc();
    }
    size = size == 0 ? 1 : size;
    if (alignment != 0) {
        const auto remainder = size % alignment;
        const auto padding = remainder == 0 ? 0 : alignment - remainder;
        if (size > std::numeric_limits<std::size_t>::max() - padding) {
            throw std::bad_alloc();
        }
        size += padding;
    }
    while (true) {
        void* result = alignment == 0 ? std::malloc(size) : std::aligned_alloc(alignment, size);
        if (result != nullptr) {
            return result;
        }
        const auto handler = std::get_new_handler();
        if (handler == nullptr) {
            throw std::bad_alloc();
        }
        handler();
    }
}

void release(void* pointer) noexcept {
    if (pointer == nullptr) {
        return;
    }
    auto& state = prism::test::detail::allocationState;
    if (state.depth != 0) {
        ++state.counts.deallocations;
    }
    std::free(pointer);
}
}

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocate(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate(size, static_cast<std::size_t>(alignment));
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return allocate(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return allocate(size); } catch (...) { return nullptr; }
}
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try { return allocate(size, static_cast<std::size_t>(alignment)); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try { return allocate(size, static_cast<std::size_t>(alignment)); } catch (...) { return nullptr; }
}
void operator delete(void* pointer) noexcept { release(pointer); }
void operator delete[](void* pointer) noexcept { release(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { release(pointer); }
void operator delete(void* pointer, std::align_val_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { release(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { release(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { release(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { release(pointer); }
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { release(pointer); }
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { release(pointer); }
