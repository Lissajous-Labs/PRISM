#include "hal/FrameGraph.h"

#include <algorithm>
#include <limits>

namespace prism::hal {
namespace {
constexpr std::uint32_t unused = std::numeric_limits<std::uint32_t>::max();

std::uint64_t aligned(std::uint64_t size) noexcept {
    return (size + heapAlignment - 1) / heapAlignment * heapAlignment;
}
}

void FrameGraph::reset() noexcept {
    textureCount_ = 0;
    passCount_ = 0;
    freeCount_ = 0;
    heapSize_ = 0;
    unaliasedSize_ = 0;
}

std::optional<FrameGraph::TextureId> FrameGraph::createTexture(TransientTexture texture) noexcept {
    if (textureCount_ == maxGraphTextures) {
        return std::nullopt;
    }
    textures_[textureCount_] = texture;
    lifetimes_[textureCount_] = {unused, 0, 0,
                                 aligned(textureBytes(texture.extent, texture.format))};
    return textureCount_++;
}

std::optional<FrameGraph::PassId>
FrameGraph::addPass(std::initializer_list<TextureId> reads,
                    std::initializer_list<TextureId> writes) noexcept {
    if (passCount_ == maxGraphPasses || reads.size() + writes.size() > maxPassBindings) {
        return std::nullopt;
    }
    Pass &pass = passes_[passCount_];
    pass.count = 0;
    for (const auto group : {reads, writes}) {
        for (const TextureId id : group) {
            if (id >= textureCount_) {
                return std::nullopt;
            }
            pass.textures[pass.count++] = id;
        }
    }
    return passCount_++;
}

bool FrameGraph::compile() noexcept {
    for (std::uint32_t pass = 0; pass < passCount_; ++pass) {
        for (std::uint32_t i = 0; i < passes_[pass].count; ++i) {
            TextureLifetime &lifetime = lifetimes_[passes_[pass].textures[i]];
            lifetime.firstPass = std::min(lifetime.firstPass, pass);
            lifetime.lastPass = std::max(lifetime.lastPass, pass);
        }
    }
    std::array<TextureId, maxGraphTextures> order{};
    std::uint32_t used = 0;
    unaliasedSize_ = 0;
    for (TextureId id = 0; id < textureCount_; ++id) {
        if (lifetimes_[id].firstPass != unused) {
            order[used++] = id;
            unaliasedSize_ += lifetimes_[id].size;
        }
    }
    std::sort(order.begin(), order.begin() + used, [&](TextureId a, TextureId b) {
        return lifetimes_[a].firstPass != lifetimes_[b].firstPass
                   ? lifetimes_[a].firstPass < lifetimes_[b].firstPass
                   : lifetimes_[a].size > lifetimes_[b].size;
    });
    freeCount_ = 0;
    heapSize_ = 0;
    std::array<TextureId, maxGraphTextures> active{};
    std::uint32_t activeCount = 0;
    for (std::uint32_t i = 0; i < used; ++i) {
        TextureLifetime &current = lifetimes_[order[i]];
        for (std::uint32_t a = 0; a < activeCount;) {
            const TextureLifetime &other = lifetimes_[active[a]];
            if (other.lastPass < current.firstPass) {
                release(other.offset, other.size);
                active[a] = active[--activeCount];
            } else {
                ++a;
            }
        }
        current.offset = allocate(current.size);
        active[activeCount++] = order[i];
    }
    return true;
}

void FrameGraph::release(std::uint64_t offset, std::uint64_t size) noexcept {
    std::uint32_t index = 0;
    while (index < freeCount_ && freeBlocks_[index].offset < offset) {
        ++index;
    }
    std::move_backward(freeBlocks_.begin() + index, freeBlocks_.begin() + freeCount_,
                       freeBlocks_.begin() + freeCount_ + 1);
    freeBlocks_[index] = {offset, size};
    ++freeCount_;
    for (std::uint32_t i = 0; i + 1 < freeCount_;) {
        if (freeBlocks_[i].offset + freeBlocks_[i].size == freeBlocks_[i + 1].offset) {
            freeBlocks_[i].size += freeBlocks_[i + 1].size;
            std::move(freeBlocks_.begin() + i + 2, freeBlocks_.begin() + freeCount_,
                      freeBlocks_.begin() + i + 1);
            --freeCount_;
        } else {
            ++i;
        }
    }
}

std::uint64_t FrameGraph::allocate(std::uint64_t size) noexcept {
    for (std::uint32_t i = 0; i < freeCount_; ++i) {
        FreeBlock &block = freeBlocks_[i];
        if (block.size >= size) {
            const std::uint64_t offset = block.offset;
            block.offset += size;
            block.size -= size;
            if (block.size == 0) {
                std::move(freeBlocks_.begin() + i + 1, freeBlocks_.begin() + freeCount_,
                          freeBlocks_.begin() + i);
                --freeCount_;
            }
            return offset;
        }
    }
    if (freeCount_ != 0) {
        FreeBlock &tail = freeBlocks_[freeCount_ - 1];
        if (tail.offset + tail.size == heapSize_) {
            const std::uint64_t offset = tail.offset;
            heapSize_ = offset + size;
            --freeCount_;
            return offset;
        }
    }
    const std::uint64_t offset = heapSize_;
    heapSize_ += size;
    return offset;
}
}
