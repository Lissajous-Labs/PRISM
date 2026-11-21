#include "Workloads.h"

#include <array>
#include <iostream>
#include <string_view>

int main(int argc, char **argv) {
    using Runner = int (*)();
    constexpr std::array<std::pair<std::string_view, Runner>, 6> workloads{{
        {"W1", prism::bench::runDense},
        {"W2", prism::bench::runWorkbench},
        {"W3", prism::bench::runPaths},
        {"W4", prism::bench::runZoom},
        {"W5", prism::bench::runEffects},
        {"W6", prism::bench::runMetering},
    }};
    int result = 0;
    for (const auto &[name, run] : workloads) {
        if (argc > 1 && std::string_view(argv[1]) != name) {
            continue;
        }
        const int code = run();
        if (code == 77) {
            std::cout << "No native GPU device available\n";
            return 77;
        }
        result = result != 0 ? result : code;
    }
    return result;
}
