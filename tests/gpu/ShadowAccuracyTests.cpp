#include "../support/Pcg.h"
#include "../support/ShadowReference.h"
#include "backends/metal/MetalBackend.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace prism;
    auto created = backends::MetalDevice::create();
    if (!created) {
        std::cout << "No native GPU device available\n";
        return 77;
    }
    auto &device = static_cast<backends::MetalDevice &>(*created);
    test::Pcg random(103);
    std::vector<backends::ShadowQuery> queries;
    for (int i = 0; i < 400; ++i) {
        const float radius = static_cast<float>(random.range(1, 30));
        const float sigma =
            static_cast<float>(std::min(radius * std::pow(10.0, random.range(-1, 2)), 300.0));
        const float hx = static_cast<float>(random.range(radius, radius + 80));
        const float hy = static_cast<float>(random.range(radius, radius + 80));
        const float px = static_cast<float>(random.range(-1.5, 1.5) * (hx + 3 * sigma));
        const float py = static_cast<float>(random.range(-1.5, 1.5) * (hy + 3 * sigma));
        queries.push_back({{px, py}, {hx, hy}, radius, sigma, {0, 0}});
    }
    std::vector<float> results(queries.size());
    if (!device.evaluateShadows(queries, results)) {
        return 1;
    }
    double worst = 0;
    for (std::size_t i = 0; i < queries.size(); ++i) {
        const auto &q = queries[i];
        const double expected = test::referenceShadow({q.point[0], q.point[1]},
                                                      {q.half[0], q.half[1]}, q.radius, q.sigma);
        worst = std::max(worst, std::abs(results[i] - expected));
    }
    std::cout << "GPU rounded-box shadow error: " << worst << '\n';
    if (worst >= 1e-4) {
        std::cerr << "GPU shadow contract failed\n";
        return 1;
    }
    return 0;
}
