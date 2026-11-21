#include "../support/AllocGuard.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <new>
#include <thread>

namespace {
void *(*volatile allocate)(std::size_t) = &::operator new;
void (*volatile release)(void *) noexcept = &::operator delete;

bool check() {
    using prism::test::AllocGuard;
    bool trapped = false;
    {
        AllocGuard outer;
        {
            AllocGuard inner;
            try {
                release(allocate(8));
            } catch (const std::bad_alloc &) {
                trapped = true;
            }
            if (!trapped || inner.counts().allocations != 1) {
                return false;
            }
        }
        if (outer.counts().allocations != 1) {
            return false;
        }
        if (::operator new(8, std::nothrow) != nullptr || outer.counts().allocations != 2) {
            return false;
        }
        if (::operator new[](64, std::align_val_t(64), std::nothrow) != nullptr ||
            outer.counts().allocations != 3) {
            return false;
        }
    }
    void *pointer = allocate(8);
    {
        AllocGuard guard;
        release(pointer);
        if (guard.counts().deallocations != 1 || guard.counts().allocations != 0) {
            return false;
        }
    }
    pointer = ::operator new(65, std::align_val_t(64));
    const bool aligned = reinterpret_cast<std::uintptr_t>(pointer) % 64 == 0;
    ::operator delete(pointer, std::align_val_t(64));
    if (!aligned) {
        return false;
    }
    std::atomic<bool> start{false};
    std::atomic<bool> finished{false};
    std::thread worker([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        release(allocate(8));
        finished.store(true, std::memory_order_release);
    });
    bool isolated = false;
    {
        AllocGuard guard;
        start.store(true, std::memory_order_release);
        while (!finished.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        isolated = guard.counts().empty();
    }
    worker.join();
    return isolated;
}
}

int main() {
    if (!check()) {
        std::cerr << "Allocation guard contract failed\n";
        return 1;
    }
    return 0;
}
