#include "widgets/VerletCableLayer.h"

#include <cmath>

namespace prism::widgets {
VerletCableLayer::VerletCableLayer(std::size_t capacity, CableSettings settings)
    : capacity_(capacity), settings_(settings), segment_(capacity), x_(capacity * cablePoints),
      y_(capacity * cablePoints), previousX_(capacity * cablePoints),
      previousY_(capacity * cablePoints), points_(capacity * cablePoints) {
    cables_.reserve(capacity);
}

std::optional<std::size_t> VerletCableLayer::connect(const NodeCard &from, std::size_t fromPort,
                                                     const NodeCard &to, std::size_t toPort) {
    std::size_t slot = 0;
    while (slot < cables_.size() && cables_[slot].active) {
        ++slot;
    }
    if (slot == cables_.size()) {
        if (cables_.size() >= capacity_) {
            return std::nullopt;
        }
        cables_.emplace_back();
    }
    cables_[slot] = {&from, fromPort, &to, toPort, true};
    const math::Vector2 start = from.portPosition(fromPort);
    const math::Vector2 end = to.portPosition(toPort);
    const double rest = math::length(end - start) * settings_.slack + 1;
    segment_[slot] = rest / static_cast<double>(cablePoints - 1);
    for (std::size_t i = 0; i < cablePoints; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(cablePoints - 1);
        const math::Vector2 point = start + (end - start) * t;
        const std::size_t lane = i * capacity_ + slot;
        x_[lane] = previousX_[lane] = point.x;
        y_[lane] = previousY_[lane] = point.y;
        points_[slot * cablePoints + i] = point;
    }
    return slot;
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
    const std::size_t lanes = cables_.size();
    const std::size_t last = (cablePoints - 1) * capacity_;
    for (std::size_t c = 0; c < lanes; ++c) {
        if (cables_[c].active) {
            const math::Vector2 start = cables_[c].from->portPosition(cables_[c].fromPort);
            const math::Vector2 end = cables_[c].to->portPosition(cables_[c].toPort);
            x_[c] = start.x;
            y_[c] = start.y;
            x_[last + c] = end.x;
            y_[last + c] = end.y;
        }
    }
    const double damping = settings_.damping;
    const double fallX = settings_.gravity.x * (dt * dt);
    const double fallY = settings_.gravity.y * (dt * dt);
    for (std::size_t lane = capacity_; lane < last; lane += capacity_) {
        double *x = x_.data() + lane;
        double *y = y_.data() + lane;
        double *px = previousX_.data() + lane;
        double *py = previousY_.data() + lane;
        for (std::size_t c = 0; c < lanes; ++c) {
            const double vx = (x[c] - px[c]) * damping;
            const double vy = (y[c] - py[c]) * damping;
            px[c] = x[c];
            py[c] = y[c];
            x[c] = x[c] + vx + fallX;
            y[c] = y[c] + vy + fallY;
        }
    }
    const double *segment = segment_.data();
    for (int iteration = 0; iteration < settings_.iterations; ++iteration) {
        for (std::size_t k = 0; k + 1 < cablePoints; ++k) {
            const std::size_t index = iteration % 2 == 0 ? k : cablePoints - 2 - k;
            double *ax = x_.data() + index * capacity_;
            double *ay = y_.data() + index * capacity_;
            double *bx = ax + capacity_;
            double *by = ay + capacity_;
            const double firstShare = index == 0 ? 0.0 : index + 2 == cablePoints ? 1.0 : 0.5;
            const double secondShare = 1.0 - firstShare;
            for (std::size_t c = 0; c < lanes; ++c) {
                const double dx = bx[c] - ax[c];
                const double dy = by[c] - ay[c];
                const double distance = std::sqrt(dx * dx + dy * dy);
                const double factor =
                    distance > segment[c] ? (distance - segment[c]) / distance : 0.0;
                const double cx = dx * factor;
                const double cy = dy * factor;
                ax[c] = ax[c] + cx * firstShare;
                ay[c] = ay[c] + cy * firstShare;
                bx[c] = bx[c] - cx * secondShare;
                by[c] = by[c] - cy * secondShare;
            }
        }
    }
    for (std::size_t c = 0; c < lanes; ++c) {
        if (!cables_[c].active) {
            continue;
        }
        for (std::size_t i = 0; i < cablePoints; ++i) {
            points_[c * cablePoints + i] = {x_[i * capacity_ + c], y_[i * capacity_ + c]};
        }
    }
}

void VerletCableLayer::paint(render::DrawList &list, std::uint32_t color, float width) const {
    for (std::size_t c = 0; c < cables_.size(); ++c) {
        if (!cables_[c].active) {
            continue;
        }
        const auto line = points(c);
        for (std::size_t i = 0; i + 1 < line.size(); ++i) {
            list.capsule(line[i], line[i + 1], width / 2, color);
        }
    }
}
}
