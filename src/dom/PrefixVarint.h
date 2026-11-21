#pragma once

#include "math/Hash64.h"

#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace prism::dom {
[[nodiscard]] constexpr std::uint64_t zigzagEncode(std::int64_t value) noexcept {
    return (static_cast<std::uint64_t>(value) << 1) ^ static_cast<std::uint64_t>(value >> 63);
}

[[nodiscard]] constexpr std::int64_t zigzagDecode(std::uint64_t value) noexcept {
    return static_cast<std::int64_t>((value >> 1) ^ (0 - (value & 1)));
}

[[nodiscard]] constexpr std::size_t varintWidth(std::uint64_t value) noexcept {
    if (value >= (std::uint64_t{1} << 56)) {
        return 9;
    }
    const auto bits = static_cast<std::size_t>(std::bit_width(value | 1));
    return (bits + 6) / 7;
}

inline void writeVarint(std::vector<std::uint8_t> &out, std::uint64_t value) {
    const std::size_t width = varintWidth(value);
    if (width == 9) {
        out.push_back(0);
        for (int i = 0; i < 8; ++i) {
            out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }
        return;
    }
    const std::uint64_t word = (value << width) | (std::uint64_t{1} << (width - 1));
    for (std::size_t i = 0; i < width; ++i) {
        out.push_back(static_cast<std::uint8_t>(word >> (8 * i)));
    }
}

class VarintReader {
  public:
    explicit VarintReader(std::span<const std::uint8_t> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] std::optional<std::uint64_t> read() noexcept {
        if (offset_ >= bytes_.size()) {
            return std::nullopt;
        }
        const std::uint64_t word = load(offset_);
        const auto first = static_cast<std::uint32_t>(word & 0xFF);
        if (first == 0) {
            if (bytes_.size() - offset_ < 9) {
                return std::nullopt;
            }
            const std::uint64_t literal = load(offset_ + 1);
            offset_ += 9;
            return literal;
        }
        const auto width = static_cast<std::size_t>(std::countr_zero(first)) + 1;
        if (bytes_.size() - offset_ < width) {
            return std::nullopt;
        }
        const std::uint64_t mask =
            width == 8 ? ~std::uint64_t{0} : (std::uint64_t{1} << (8 * width)) - 1;
        offset_ += width;
        return (word & mask) >> width;
    }

    [[nodiscard]] std::optional<std::span<const std::uint8_t>> bytes(std::size_t count) noexcept {
        if (bytes_.size() - offset_ < count) {
            return std::nullopt;
        }
        const auto view = bytes_.subspan(offset_, count);
        offset_ += count;
        return view;
    }

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    void seek(std::size_t offset) noexcept { offset_ = offset; }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;

    [[nodiscard]] std::uint64_t load(std::size_t at) const noexcept {
        return math::detail::loadWord(bytes_, at);
    }
};

[[nodiscard]] constexpr std::uint32_t crc32cChecksum(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t crc = 0xFFFFFFFFU;
    std::size_t offset = 0;
    for (; bytes.size() - offset >= 8; offset += 8) {
        crc = math::crc32c(crc, math::detail::loadWord(bytes, offset));
    }
    for (; offset < bytes.size(); ++offset) {
        crc = (crc >> 8) ^ math::detail::crcTable[(crc ^ bytes[offset]) & 0xFFU];
    }
    return crc ^ 0xFFFFFFFFU;
}
}
