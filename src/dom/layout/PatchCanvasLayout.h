#pragma once

#include "math/Vector2.h"

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace prism::dom::layout {
inline constexpr float snapRadius = 18;
inline constexpr float claspGap = 2;
inline constexpr float tearOffThreshold = 28;
inline constexpr float minimumZoom = 0.25f;
inline constexpr float maximumZoom = 3.0f;
inline constexpr float cullCell = 256;

struct WorldRect {
    float x0;
    float y0;
    float x1;
    float y1;

    [[nodiscard]] bool overlaps(const WorldRect &o) const noexcept {
        return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1;
    }
};

class CanvasCamera {
  public:
    CanvasCamera(math::Vector2 center, float scale) : center_(center), scale_(scale) {}

    [[nodiscard]] math::Vector2 toScreen(math::Vector2 world) const noexcept {
        return (world - center_) * scale_;
    }
    [[nodiscard]] math::Vector2 toWorld(math::Vector2 screen) const noexcept {
        return center_ + screen * (1.0 / scale_);
    }
    void zoomAt(math::Vector2 screenCursor, float factor) noexcept;
    void pan(math::Vector2 screenDelta) noexcept {
        center_ = center_ - screenDelta * (1.0 / scale_);
    }
    [[nodiscard]] float scale() const noexcept { return scale_; }
    [[nodiscard]] math::Vector2 center() const noexcept { return center_; }

  private:
    math::Vector2 center_;
    float scale_;
};

using ModuleId = std::uint32_t;

enum class DragMode : std::uint8_t { group, single };

struct DragState {
    ModuleId module;
    DragMode mode;
    math::Vector2 pointerStart;
    bool tornOff;
    std::optional<ModuleId> snapTarget;
};

class PatchCanvas {
  public:
    explicit PatchCanvas(CanvasCamera camera) : camera_(camera) {}

    [[nodiscard]] ModuleId add(WorldRect rect);
    [[nodiscard]] const WorldRect &rect(ModuleId id) const noexcept { return modules_[id].rect; }
    [[nodiscard]] std::uint32_t groupOf(ModuleId id) const noexcept { return find(id); }
    [[nodiscard]] bool docked(ModuleId a, ModuleId b) const noexcept { return find(a) == find(b); }
    [[nodiscard]] CanvasCamera &camera() noexcept { return camera_; }

    void beginDrag(ModuleId id, math::Vector2 screenPointer, DragMode mode);
    void dragTo(math::Vector2 screenPointer);
    void endDrag();

    void visible(const WorldRect &viewport, std::vector<ModuleId> &out) const;

  private:
    struct Module {
        WorldRect rect;
        WorldRect origin;
        std::uint32_t group;
    };

    CanvasCamera camera_;
    std::vector<Module> modules_;
    std::optional<DragState> drag_;
    std::unordered_map<std::uint64_t, std::vector<ModuleId>> cells_;

    [[nodiscard]] std::uint32_t find(ModuleId id) const noexcept;
    void detach(ModuleId id);
    void merge(ModuleId a, ModuleId b);
    void moveTo(ModuleId id, const WorldRect &rect);
    void index(ModuleId id, bool insert);
    [[nodiscard]] bool dragged(ModuleId id) const noexcept;
};

[[nodiscard]] constexpr std::uint32_t deckColumns(std::uint32_t controls) noexcept {
    if (controls <= 4) {
        return controls;
    }
    if (controls <= 8) {
        return (controls + 1) / 2;
    }
    return 4;
}

struct DeckSlot {
    float x0;
    float y0;
    float x1;
    float y1;
};

template <std::size_t N> struct Deck {
    std::array<DeckSlot, N> slots{};
    std::uint32_t count = 0;
};

[[nodiscard]] constexpr Deck<16> deckLayout(std::uint32_t controls, float width, float height,
                                            float padding) noexcept {
    Deck<16> deck;
    if (controls == 0 || controls > 16) {
        return deck;
    }
    const std::uint32_t columns = deckColumns(controls);
    const std::uint32_t rows = (controls + columns - 1) / columns;
    const float cellWidth =
        (width - padding * static_cast<float>(columns + 1)) / static_cast<float>(columns);
    const float cellHeight =
        (height - padding * static_cast<float>(rows + 1)) / static_cast<float>(rows);
    for (std::uint32_t i = 0; i < controls; ++i) {
        const std::uint32_t row = i / columns;
        const std::uint32_t column = i % columns;
        const float x0 = padding + static_cast<float>(column) * (cellWidth + padding);
        const float y0 = padding + static_cast<float>(row) * (cellHeight + padding);
        deck.slots[i] = {x0, y0, x0 + cellWidth, y0 + cellHeight};
    }
    deck.count = controls;
    return deck;
}
}
