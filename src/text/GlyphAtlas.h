#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace prism::text {
inline constexpr std::array<std::uint32_t, 7> shelfHeights{16, 24, 32, 48, 64, 96, 128};
inline constexpr std::uint32_t atlasTile = 64;
inline constexpr std::uint32_t atlasPages = 8;

struct AtlasSlot {
    std::uint32_t page;
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t width;
    std::uint32_t height;
};

class GlyphAtlas {
  public:
    GlyphAtlas(std::uint32_t pageSize, std::uint32_t channels, std::uint32_t maxPages);

    [[nodiscard]] std::optional<AtlasSlot> find(std::uint64_t key, std::uint64_t frame);
    [[nodiscard]] std::optional<AtlasSlot> insert(std::uint64_t key, std::uint32_t width,
                                                  std::uint32_t height,
                                                  std::span<const float> texels,
                                                  std::uint64_t frame);

    [[nodiscard]] std::uint32_t pageSize() const noexcept { return pageSize_; }
    [[nodiscard]] std::uint32_t channels() const noexcept { return channels_; }
    [[nodiscard]] std::size_t pageCount() const noexcept { return pageCount_; }
    [[nodiscard]] std::size_t maxPages() const noexcept { return maxPages_; }
    [[nodiscard]] std::span<const float> page(std::size_t index) const noexcept {
        return std::span(storage_).subspan(index * pageTexels(), pageTexels());
    }
    [[nodiscard]] std::span<const float> texels() const noexcept { return storage_; }
    [[nodiscard]] std::span<const std::uint64_t> tiles() const noexcept { return tiles_; }
    [[nodiscard]] std::uint64_t modification() const noexcept { return modification_; }
    [[nodiscard]] std::size_t evictions() const noexcept { return evictions_; }

  private:
    struct Shelf {
        std::uint32_t page;
        std::uint32_t y;
        std::uint32_t heightClass;
        std::uint32_t cursor;
        std::uint32_t generation;
        std::uint64_t lastUsed;
    };
    struct Entry {
        AtlasSlot slot;
        std::uint32_t shelf;
        std::uint32_t generation;
    };

    std::uint32_t pageSize_;
    std::uint32_t channels_;
    std::uint32_t maxPages_;
    std::uint32_t tilesPerSide_;
    std::size_t pageCount_ = 0;
    std::vector<float> storage_;
    std::vector<std::uint64_t> tiles_;
    std::uint64_t modification_ = 0;
    std::vector<std::uint32_t> pageFill_;
    std::vector<Shelf> shelves_;
    std::array<std::vector<std::uint32_t>, shelfHeights.size()> classShelves_;
    std::array<std::uint64_t, shelfHeights.size()> available_{};
    std::unordered_map<std::uint64_t, Entry> entries_;
    std::size_t evictions_ = 0;

    [[nodiscard]] std::size_t pageTexels() const noexcept {
        return static_cast<std::size_t>(pageSize_) * pageSize_ * channels_;
    }
    [[nodiscard]] std::optional<std::uint32_t> openShelf(std::uint32_t heightClass);
    [[nodiscard]] std::uint32_t evictShelf(std::uint32_t heightClass);
    void blit(const AtlasSlot &slot, std::span<const float> texels);
};
}
