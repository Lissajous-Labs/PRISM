#pragma once

#include "math/SimdVec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

namespace prism::hal {
inline constexpr std::size_t stagingAlignment = 64;

class AlignedBuffer {
  public:
    explicit AlignedBuffer(std::size_t bytes)
        : bytes_(bytes), data_(static_cast<std::byte *>(
                             ::operator new(bytes, std::align_val_t(stagingAlignment)))) {}
    ~AlignedBuffer() { ::operator delete(data_, std::align_val_t(stagingAlignment)); }
    AlignedBuffer(const AlignedBuffer &) = delete;
    AlignedBuffer &operator=(const AlignedBuffer &) = delete;

    [[nodiscard]] std::byte *data() noexcept { return data_; }
    [[nodiscard]] const std::byte *data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_; }

  private:
    std::size_t bytes_;
    std::byte *data_;
};

template <std::size_t Frames> class FrameRing {
  public:
    explicit FrameRing(std::size_t bytesPerFrame)
        : buffers_{makeBuffers(bytesPerFrame, std::make_index_sequence<Frames>{})} {}

    void beginFrame() noexcept {
        frame_ = (frame_ + 1) % Frames;
        cursor_ = 0;
    }

    [[nodiscard]] bool streamRecord(const void *record) noexcept {
        AlignedBuffer &buffer = *buffers_[frame_];
        if (cursor_ + stagingAlignment > buffer.size()) {
            return false;
        }
        using Lane = math::SimdVec<std::uint32_t, 8>;
        std::array<std::uint32_t, 16> words;
        std::memcpy(words.data(), record, stagingAlignment);
        auto *destination = reinterpret_cast<std::uint32_t *>(buffer.data() + cursor_);
        Lane(std::array<std::uint32_t, 8>{words[0], words[1], words[2], words[3], words[4],
                                          words[5], words[6], words[7]})
            .streamStore(destination);
        Lane(std::array<std::uint32_t, 8>{words[8], words[9], words[10], words[11], words[12],
                                          words[13], words[14], words[15]})
            .streamStore(destination + 8);
        cursor_ += stagingAlignment;
        return true;
    }

    void finishFrame() noexcept { math::SimdVec<std::uint32_t, 8>::finishStreamStores(); }

    [[nodiscard]] std::size_t frameIndex() const noexcept { return frame_; }
    [[nodiscard]] std::size_t bytesWritten() const noexcept { return cursor_; }
    [[nodiscard]] const std::byte *frameData() const noexcept { return buffers_[frame_]->data(); }

  private:
    std::array<std::unique_ptr<AlignedBuffer>, Frames> buffers_;
    std::size_t frame_ = Frames - 1;
    std::size_t cursor_ = 0;

    template <std::size_t... Index>
    static std::array<std::unique_ptr<AlignedBuffer>, Frames>
    makeBuffers(std::size_t bytes, std::index_sequence<Index...>) {
        return {((void)Index, std::make_unique<AlignedBuffer>(bytes))...};
    }
};
}
