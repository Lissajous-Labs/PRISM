#pragma once

#include <cstddef>

namespace prism::test {
struct AllocationCounts {
    std::size_t allocations;
    std::size_t deallocations;

    [[nodiscard]] bool empty() const noexcept {
        return allocations == 0 && deallocations == 0;
    }
};

namespace detail {
struct AllocationState {
    std::size_t depth = 0;
    AllocationCounts counts{};
};
extern thread_local AllocationState allocationState;
}

class AllocGuard final {
public:
    AllocGuard() noexcept : initial_(detail::allocationState.counts) {
        ++detail::allocationState.depth;
    }

    ~AllocGuard() {
        --detail::allocationState.depth;
    }

    AllocGuard(const AllocGuard&) = delete;
    AllocGuard& operator=(const AllocGuard&) = delete;

    [[nodiscard]] AllocationCounts counts() const noexcept {
        const auto current = detail::allocationState.counts;
        return {current.allocations - initial_.allocations, current.deallocations - initial_.deallocations};
    }

private:
    AllocationCounts initial_;
};
}
