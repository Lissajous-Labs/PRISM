#include "dom/layout/PatchCanvasLayout.h"

#include <algorithm>
#include <cmath>

namespace prism::dom::layout {
namespace {
std::uint64_t cellKey(std::int64_t x, std::int64_t y) noexcept {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) |
           static_cast<std::uint32_t>(y);
}

std::int64_t cellOf(float value) noexcept {
    return static_cast<std::int64_t>(std::floor(value / cullCell));
}

WorldRect translated(const WorldRect &rect, double dx, double dy) {
    return {static_cast<float>(rect.x0 + dx), static_cast<float>(rect.y0 + dy),
            static_cast<float>(rect.x1 + dx), static_cast<float>(rect.y1 + dy)};
}
}

void CanvasCamera::zoomAt(math::Vector2 screenCursor, float factor) noexcept {
    const math::Vector2 anchor = toWorld(screenCursor);
    scale_ = std::clamp(scale_ * factor, minimumZoom, maximumZoom);
    center_ = anchor - screenCursor * (1.0 / scale_);
}

ModuleId PatchCanvas::add(WorldRect rect) {
    const auto id = static_cast<ModuleId>(modules_.size());
    modules_.push_back({rect, rect, id});
    index(id, true);
    return id;
}

std::uint32_t PatchCanvas::find(ModuleId id) const noexcept { return modules_[id].group; }

void PatchCanvas::detach(ModuleId id) {
    std::uint32_t fresh = 0;
    for (const Module &module : modules_) {
        fresh = std::max(fresh, module.group + 1);
    }
    modules_[id].group = fresh;
}

void PatchCanvas::merge(ModuleId a, ModuleId b) {
    const std::uint32_t from = modules_[b].group;
    const std::uint32_t to = modules_[a].group;
    for (Module &module : modules_) {
        if (module.group == from) {
            module.group = to;
        }
    }
}

void PatchCanvas::index(ModuleId id, bool insert) {
    const WorldRect &r = modules_[id].rect;
    for (std::int64_t y = cellOf(r.y0); y <= cellOf(r.y1); ++y) {
        for (std::int64_t x = cellOf(r.x0); x <= cellOf(r.x1); ++x) {
            auto &bucket = cells_[cellKey(x, y)];
            if (insert) {
                bucket.push_back(id);
            } else {
                bucket.erase(std::remove(bucket.begin(), bucket.end(), id), bucket.end());
            }
        }
    }
}

void PatchCanvas::moveTo(ModuleId id, const WorldRect &rect) {
    index(id, false);
    modules_[id].rect = rect;
    index(id, true);
}

bool PatchCanvas::dragged(ModuleId id) const noexcept {
    if (!drag_) {
        return false;
    }
    if (drag_->mode == DragMode::single || drag_->tornOff) {
        return id == drag_->module;
    }
    return find(id) == find(drag_->module);
}

void PatchCanvas::beginDrag(ModuleId id, math::Vector2 screenPointer, DragMode mode) {
    for (Module &module : modules_) {
        module.origin = module.rect;
    }
    bool alone = true;
    for (ModuleId other = 0; other < modules_.size(); ++other) {
        alone = alone && (other == id || find(other) != find(id));
    }
    drag_ = DragState{id, mode, screenPointer, mode == DragMode::single && alone, std::nullopt};
}

void PatchCanvas::dragTo(math::Vector2 screenPointer) {
    if (!drag_) {
        return;
    }
    const math::Vector2 screenDelta = screenPointer - drag_->pointerStart;
    if (drag_->mode == DragMode::single && !drag_->tornOff) {
        if (math::length(screenDelta) <= tearOffThreshold) {
            return;
        }
        drag_->tornOff = true;
        detach(drag_->module);
    }
    const double scale = camera_.scale();
    const double dx = screenDelta.x / scale;
    const double dy = screenDelta.y / scale;
    const WorldRect candidate = translated(modules_[drag_->module].origin, dx, dy);
    const double radius = snapRadius / scale;
    double bestX = radius;
    double bestY = radius;
    double snapX = 0;
    double snapY = 0;
    std::optional<ModuleId> targetX;
    std::optional<ModuleId> targetY;
    for (ModuleId other = 0; other < modules_.size(); ++other) {
        if (dragged(other)) {
            continue;
        }
        const WorldRect &o = modules_[other].rect;
        const bool verticalOverlap = candidate.y0 < o.y1 + radius && o.y0 - radius < candidate.y1;
        const bool horizontalOverlap = candidate.x0 < o.x1 + radius && o.x0 - radius < candidate.x1;
        const auto consider = [&](double delta, double &best, double &snap,
                                  std::optional<ModuleId> &target, bool clasp) {
            if (std::abs(delta) <= best) {
                best = std::abs(delta);
                snap = delta;
                target = clasp ? std::optional<ModuleId>(other) : std::nullopt;
            }
        };
        if (verticalOverlap) {
            consider((o.x1 + claspGap) - candidate.x0, bestX, snapX, targetX, true);
            consider((o.x0 - claspGap) - candidate.x1, bestX, snapX, targetX, true);
            consider(o.y0 - candidate.y0, bestY, snapY, targetY, false);
        }
        if (horizontalOverlap) {
            consider((o.y1 + claspGap) - candidate.y0, bestY, snapY, targetY, true);
            consider((o.y0 - claspGap) - candidate.y1, bestY, snapY, targetY, true);
            consider(o.x0 - candidate.x0, bestX, snapX, targetX, false);
        }
    }
    drag_->snapTarget = targetX ? targetX : targetY;
    for (ModuleId id = 0; id < modules_.size(); ++id) {
        if (dragged(id)) {
            moveTo(id, translated(modules_[id].origin, dx + snapX, dy + snapY));
        }
    }
}

void PatchCanvas::endDrag() {
    if (drag_ && drag_->snapTarget) {
        merge(*drag_->snapTarget, drag_->module);
    }
    drag_.reset();
}

void PatchCanvas::visible(const WorldRect &viewport, std::vector<ModuleId> &out) const {
    out.clear();
    for (std::int64_t y = cellOf(viewport.y0); y <= cellOf(viewport.y1); ++y) {
        for (std::int64_t x = cellOf(viewport.x0); x <= cellOf(viewport.x1); ++x) {
            const auto found = cells_.find(cellKey(x, y));
            if (found == cells_.end()) {
                continue;
            }
            for (const ModuleId id : found->second) {
                if (modules_[id].rect.overlaps(viewport)) {
                    out.push_back(id);
                }
            }
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}
}
