#include <cfenv>
#include <cstdint>
#include <iostream>
#include <limits>

static_assert(sizeof(std::uint32_t) == 4);
static_assert(sizeof(std::uint64_t) == 8);
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
static_assert(std::numeric_limits<float>::radix == 2);
static_assert(std::numeric_limits<double>::digits == 53);

int main() {
    if (std::fegetround() != FE_TONEAREST) {
        std::cerr << "Round-to-nearest arithmetic is required\n";
        return 1;
    }
    return 0;
}
