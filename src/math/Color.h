#pragma once

namespace prism::math {
struct LinearColor {
    double r;
    double g;
    double b;
    double a;
};

struct PremultipliedColor {
    double r;
    double g;
    double b;
    double a;

    friend constexpr bool operator==(const PremultipliedColor &,
                                     const PremultipliedColor &) = default;
};

[[nodiscard]] constexpr PremultipliedColor premultiply(LinearColor color) noexcept {
    return {color.r * color.a, color.g * color.a, color.b * color.a, color.a};
}

[[nodiscard]] constexpr LinearColor unpremultiply(PremultipliedColor color) noexcept {
    if (color.a == 0) {
        return {0, 0, 0, 0};
    }
    return {color.r / color.a, color.g / color.a, color.b / color.a, color.a};
}

[[nodiscard]] constexpr PremultipliedColor withCoverage(PremultipliedColor color,
                                                        double coverage) noexcept {
    return {color.r * coverage, color.g * coverage, color.b * coverage, color.a * coverage};
}

[[nodiscard]] constexpr PremultipliedColor sourceOver(PremultipliedColor source,
                                                      PremultipliedColor destination) noexcept {
    const double keep = 1 - source.a;
    return {source.r + keep * destination.r, source.g + keep * destination.g,
            source.b + keep * destination.b, source.a + keep * destination.a};
}

[[nodiscard]] constexpr PremultipliedColor screen(PremultipliedColor source,
                                                  PremultipliedColor destination) noexcept {
    return {source.r + destination.r - source.r * destination.r,
            source.g + destination.g - source.g * destination.g,
            source.b + destination.b - source.b * destination.b,
            source.a + destination.a - source.a * destination.a};
}

[[nodiscard]] constexpr PremultipliedColor multiply(PremultipliedColor source,
                                                    PremultipliedColor destination) noexcept {
    const double uncoveredDestination = 1 - destination.a;
    const double uncoveredSource = 1 - source.a;
    const auto channel = [&](double s, double d) {
        return s * d + uncoveredDestination * s + uncoveredSource * d;
    };
    return {channel(source.r, destination.r), channel(source.g, destination.g),
            channel(source.b, destination.b), source.a + destination.a * uncoveredSource};
}
}
