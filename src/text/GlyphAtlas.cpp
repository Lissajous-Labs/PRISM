#include "text/GlyphAtlas.h"

#include <algorithm>
#include <bit>
#include <limits>

namespace prism::text {
GlyphAtlas::GlyphAtlas(std::uint32_t pageSize, std::uint32_t channels, std::uint32_t maxPages)
    : pageSize_(pageSize), channels_(channels), maxPages_(maxPages),
      tilesPerSide_((pageSize + atlasTile - 1) / atlasTile) {
    storage_.reserve(pageTexels() * maxPages_);
}

std::optional<AtlasSlot> GlyphAtlas::find(std::uint64_t key, std::uint64_t frame) {
    const auto found = entries_.find(key);
    if (found == entries_.end()) {
        return std::nullopt;
    }
    Shelf &shelf = shelves_[found->second.shelf];
    if (shelf.generation != found->second.generation) {
        entries_.erase(found);
        return std::nullopt;
    }
    shelf.lastUsed = frame;
    return found->second.slot;
}

std::optional<std::uint32_t> GlyphAtlas::openShelf(std::uint32_t heightClass) {
    const std::uint32_t height = shelfHeights[heightClass];
    auto &owned = classShelves_[heightClass];
    if (owned.size() >= 64) {
        return std::nullopt;
    }
    for (std::uint32_t page = 0; page < pageCount_; ++page) {
        if (pageFill_[page] + height <= pageSize_) {
            shelves_.push_back({page, pageFill_[page], heightClass, 0, 0, 0});
            pageFill_[page] += height;
            owned.push_back(static_cast<std::uint32_t>(shelves_.size() - 1));
            return static_cast<std::uint32_t>(owned.size() - 1);
        }
    }
    if (pageCount_ < maxPages_) {
        ++pageCount_;
        storage_.resize(pageTexels() * pageCount_, 0.0f);
        tiles_.resize(static_cast<std::size_t>(tilesPerSide_) * tilesPerSide_ * pageCount_, 0);
        pageFill_.push_back(0);
        return openShelf(heightClass);
    }
    return std::nullopt;
}

std::uint32_t GlyphAtlas::evictShelf(std::uint32_t heightClass) {
    const auto &owned = classShelves_[heightClass];
    std::uint32_t coldest = 0;
    std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
    for (std::uint32_t i = 0; i < owned.size(); ++i) {
        if (shelves_[owned[i]].lastUsed < oldest) {
            oldest = shelves_[owned[i]].lastUsed;
            coldest = i;
        }
    }
    Shelf &shelf = shelves_[owned[coldest]];
    shelf.cursor = 0;
    ++shelf.generation;
    ++evictions_;
    return coldest;
}

std::optional<AtlasSlot> GlyphAtlas::insert(std::uint64_t key, std::uint32_t width,
                                            std::uint32_t height, std::span<const float> texels,
                                            std::uint64_t frame) {
    const auto heightClass = static_cast<std::uint32_t>(
        std::lower_bound(shelfHeights.begin(), shelfHeights.end(), height) - shelfHeights.begin());
    if (heightClass >= shelfHeights.size() || width > pageSize_ || width == 0 || height == 0 ||
        texels.size() < static_cast<std::size_t>(width) * height * channels_) {
        return std::nullopt;
    }
    std::uint64_t &mask = available_[heightClass];
    std::uint32_t local = 0;
    if (mask != 0) {
        local = static_cast<std::uint32_t>(std::countr_zero(mask));
    } else if (const auto opened = openShelf(heightClass)) {
        local = *opened;
    } else if (!classShelves_[heightClass].empty()) {
        local = evictShelf(heightClass);
    } else {
        return std::nullopt;
    }
    mask |= std::uint64_t{1} << local;
    const std::uint32_t index = classShelves_[heightClass][local];
    Shelf &shelf = shelves_[index];
    if (shelf.cursor + width > pageSize_) {
        mask &= ~(std::uint64_t{1} << local);
        return insert(key, width, height, texels, frame);
    }
    const AtlasSlot slot{shelf.page, shelf.cursor, shelf.y, width, height};
    shelf.cursor += width + 1;
    shelf.lastUsed = frame;
    if (shelf.cursor >= pageSize_) {
        mask &= ~(std::uint64_t{1} << local);
    }
    blit(slot, texels);
    entries_[key] = {slot, index, shelf.generation};
    return slot;
}

void GlyphAtlas::blit(const AtlasSlot &slot, std::span<const float> texels) {
    float *page = storage_.data() + slot.page * pageTexels();
    for (std::uint32_t y = 0; y < slot.height; ++y) {
        for (std::uint32_t x = 0; x < slot.width; ++x) {
            for (std::uint32_t c = 0; c < channels_; ++c) {
                page[((static_cast<std::size_t>(slot.y) + y) * pageSize_ + slot.x + x) * channels_ +
                     c] = texels[(static_cast<std::size_t>(y) * slot.width + x) * channels_ + c];
            }
        }
    }

    ++modification_;
    const std::size_t base = static_cast<std::size_t>(slot.page) * tilesPerSide_ * tilesPerSide_;
    for (std::uint32_t ty = slot.y / atlasTile; ty <= (slot.y + slot.height - 1) / atlasTile;
         ++ty) {
        for (std::uint32_t tx = slot.x / atlasTile; tx <= (slot.x + slot.width - 1) / atlasTile;
             ++tx) {
            tiles_[base + static_cast<std::size_t>(ty) * tilesPerSide_ + tx] = modification_;
        }
    }
}
}
