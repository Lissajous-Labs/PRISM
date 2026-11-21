#pragma once

#include "math/SpringCable.h"
#include "widgets/NodeCard.h"

#include <optional>
#include <vector>

namespace prism::widgets {
inline constexpr std::size_t cablePoints = 24;

struct CableSettings {
    math::Vector2 gravity{0, 1400};
    double damping = 0.985;
    int iterations = 24;
    double slack = 1.12;
};

class VerletCableLayer {
  public:
    explicit VerletCableLayer(std::size_t capacity, CableSettings settings = {});

    [[nodiscard]] std::optional<std::size_t> connect(const NodeCard &from, std::size_t fromPort,
                                                     const NodeCard &to, std::size_t toPort);
    void disconnect(std::size_t cable) noexcept;
    void step(double dt) noexcept;
    void paint(render::DrawList &list, std::uint32_t color, float width) const;

    [[nodiscard]] bool active(std::size_t cable) const noexcept { return cables_[cable].active; }
    [[nodiscard]] std::span<const math::Vector2> points(std::size_t cable) const noexcept {
        return cables_[cable].body.points();
    }
    [[nodiscard]] std::size_t count() const noexcept;

  private:
    struct Cable {
        math::VerletCable<cablePoints> body;
        const NodeCard *from;
        std::size_t fromPort;
        const NodeCard *to;
        std::size_t toPort;
        bool active;
    };

    std::size_t capacity_;
    CableSettings settings_;
    std::vector<Cable> cables_;
};
}
