#include "widgets/VerletCableLayer.h"

namespace prism::widgets {
VerletCableLayer::VerletCableLayer(std::size_t capacity, CableSettings settings)
    : capacity_(capacity), settings_(settings) {
    cables_.reserve(capacity);
}

std::optional<std::size_t> VerletCableLayer::connect(const NodeCard &from, std::size_t fromPort,
                                                     const NodeCard &to, std::size_t toPort) {
    const math::Vector2 start = from.portPosition(fromPort);
    const math::Vector2 end = to.portPosition(toPort);
    const double rest = math::length(end - start) * settings_.slack + 1;
    for (std::size_t i = 0; i < cables_.size(); ++i) {
        if (!cables_[i].active) {
            cables_[i] = {math::VerletCable<cablePoints>(start, end, rest),
                          &from,
                          fromPort,
                          &to,
                          toPort,
                          true};
            return i;
        }
    }
    if (cables_.size() >= capacity_) {
        return std::nullopt;
    }
    cables_.push_back(
        {math::VerletCable<cablePoints>(start, end, rest), &from, fromPort, &to, toPort, true});
    return cables_.size() - 1;
}

void VerletCableLayer::disconnect(std::size_t cable) noexcept { cables_[cable].active = false; }

std::size_t VerletCableLayer::count() const noexcept {
    std::size_t total = 0;
    for (const Cable &cable : cables_) {
        total += cable.active ? 1 : 0;
    }
    return total;
}

void VerletCableLayer::step(double dt) noexcept {
    for (Cable &cable : cables_) {
        if (!cable.active) {
            continue;
        }
        cable.body.setEnds(cable.from->portPosition(cable.fromPort),
                           cable.to->portPosition(cable.toPort));
        cable.body.step(dt, settings_.gravity, settings_.damping, settings_.iterations);
    }
}

void VerletCableLayer::paint(render::DrawList &list, std::uint32_t color, float width) const {
    for (const Cable &cable : cables_) {
        if (!cable.active) {
            continue;
        }
        const auto &points = cable.body.points();
        for (std::size_t i = 0; i + 1 < points.size(); ++i) {
            list.capsule(points[i], points[i + 1], width / 2, color);
        }
    }
}
}
