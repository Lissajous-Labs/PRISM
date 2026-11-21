#pragma once

#include "math/Affine2D.h"

#include <cstdint>
#include <optional>

namespace prism::render {
inline constexpr std::uint16_t identityTransform = 0;
inline constexpr std::uint16_t noClip = 0;
inline constexpr std::uint32_t noTexture = 0xFFFFFFFFu;

struct TransformRecord {
    float forward[6];
    float inverse[6];

    [[nodiscard]] static std::optional<TransformRecord>
    from(const math::Affine2D &transform) noexcept {
        const auto inverted = transform.inverse();
        if (!inverted) {
            return std::nullopt;
        }
        return TransformRecord{{static_cast<float>(transform.a), static_cast<float>(transform.b),
                                static_cast<float>(transform.c), static_cast<float>(transform.d),
                                static_cast<float>(transform.tx), static_cast<float>(transform.ty)},
                               {static_cast<float>(inverted->a), static_cast<float>(inverted->b),
                                static_cast<float>(inverted->c), static_cast<float>(inverted->d),
                                static_cast<float>(inverted->tx),
                                static_cast<float>(inverted->ty)}};
    }

    [[nodiscard]] math::Affine2D forwardTransform() const noexcept {
        return {forward[0], forward[1], forward[2], forward[3], forward[4], forward[5]};
    }
    [[nodiscard]] math::Affine2D inverseTransform() const noexcept {
        return {inverse[0], inverse[1], inverse[2], inverse[3], inverse[4], inverse[5]};
    }
};

static_assert(sizeof(TransformRecord) == 48);

enum class ClipKind : std::uint32_t { none = 0, rect = 1, roundedRect = 2, ellipse = 3 };

struct ClipRecord {
    std::uint16_t parent;
    std::uint16_t transformId;
    ClipKind kind;
    float bounds[4];
    float radii[4];
};

static_assert(sizeof(ClipRecord) == 40);
}
