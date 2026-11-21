#pragma once

#include "hal/HalTypes.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>

namespace prism::hal {
inline constexpr std::uint32_t maxGraphPasses = 64;
inline constexpr std::uint32_t maxGraphTextures = 64;
inline constexpr std::uint32_t maxPassBindings = 8;
inline constexpr std::uint64_t heapAlignment = 256;

struct TransientTexture {
    Extent extent;
    PixelFormat format;
};

struct TextureLifetime {
    std::uint32_t firstPass;
    std::uint32_t lastPass;
    std::uint64_t offset;
    std::uint64_t size;
};

class FrameGraph {
  public:
    using TextureId = std::uint32_t;
    using PassId = std::uint32_t;

    void reset() noexcept;
    [[nodiscard]] std::optional<TextureId> createTexture(TransientTexture texture) noexcept;
    [[nodiscard]] std::optional<PassId> addPass(std::initializer_list<TextureId> reads,
                                                std::initializer_list<TextureId> writes) noexcept;
    [[nodiscard]] bool compile() noexcept;

    [[nodiscard]] std::uint32_t textureCount() const noexcept { return textureCount_; }
    [[nodiscard]] std::uint32_t passCount() const noexcept { return passCount_; }
    [[nodiscard]] const TextureLifetime &lifetime(TextureId id) const noexcept {
        return lifetimes_[id];
    }
    [[nodiscard]] std::uint64_t heapSize() const noexcept { return heapSize_; }
    [[nodiscard]] std::uint64_t unaliasedSize() const noexcept { return unaliasedSize_; }

  private:
    struct Pass {
        std::array<TextureId, maxPassBindings> textures;
        std::uint32_t count;
    };
    struct FreeBlock {
        std::uint64_t offset;
        std::uint64_t size;
    };

    std::array<TransientTexture, maxGraphTextures> textures_{};
    std::array<TextureLifetime, maxGraphTextures> lifetimes_{};
    std::array<Pass, maxGraphPasses> passes_{};
    std::array<FreeBlock, maxGraphTextures + 1> freeBlocks_{};
    std::uint32_t textureCount_ = 0;
    std::uint32_t passCount_ = 0;
    std::uint32_t freeCount_ = 0;
    std::uint64_t heapSize_ = 0;
    std::uint64_t unaliasedSize_ = 0;

    void release(std::uint64_t offset, std::uint64_t size) noexcept;
    [[nodiscard]] std::uint64_t allocate(std::uint64_t size) noexcept;
};
}
