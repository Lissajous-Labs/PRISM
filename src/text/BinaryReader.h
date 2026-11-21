#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace prism::text {
class BinaryReader {
  public:
    explicit BinaryReader(std::span<const std::uint8_t> data, std::size_t offset = 0) noexcept
        : data_(data), offset_(offset) {}

    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    void seek(std::size_t offset) noexcept { offset_ = offset; }
    void skip(std::size_t bytes) noexcept { offset_ += bytes; }

    [[nodiscard]] std::uint8_t u8() noexcept { return static_cast<std::uint8_t>(read(1)); }
    [[nodiscard]] std::uint16_t u16() noexcept { return static_cast<std::uint16_t>(read(2)); }
    [[nodiscard]] std::int16_t i16() noexcept { return static_cast<std::int16_t>(read(2)); }
    [[nodiscard]] std::uint32_t u32() noexcept { return static_cast<std::uint32_t>(read(4)); }

    [[nodiscard]] std::uint16_t u16At(std::size_t offset) noexcept {
        seek(offset);
        return u16();
    }

  private:
    std::span<const std::uint8_t> data_;
    std::size_t offset_;
    bool ok_ = true;

    std::uint32_t read(std::size_t bytes) noexcept {
        if (offset_ + bytes > data_.size()) {
            ok_ = false;
            offset_ += bytes;
            return 0;
        }
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < bytes; ++i) {
            value = (value << 8) | data_[offset_ + i];
        }
        offset_ += bytes;
        return value;
    }
};

[[nodiscard]] constexpr std::uint32_t tag(const char (&name)[5]) noexcept {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(name[0])) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(name[1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(name[2])) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(name[3]));
}
}
