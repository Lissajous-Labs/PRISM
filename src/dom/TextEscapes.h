#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace prism::dom {
inline bool appendUtf8(std::string &out, std::uint32_t codepoint) {
    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        return false;
    }
    if (codepoint < 0x80) {
        out += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codepoint >> 18));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    return true;
}

inline std::optional<std::uint32_t> parseNumber(std::string_view digits, std::uint32_t base) {
    if (digits.empty() || digits.size() > 8) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (const char c : digits) {
        std::uint32_t digit;
        if (c >= '0' && c <= '9') {
            digit = static_cast<std::uint32_t>(c - '0');
        } else if (base == 16 && c >= 'a' && c <= 'f') {
            digit = static_cast<std::uint32_t>(c - 'a' + 10);
        } else if (base == 16 && c >= 'A' && c <= 'F') {
            digit = static_cast<std::uint32_t>(c - 'A' + 10);
        } else {
            return std::nullopt;
        }
        if (digit >= base) {
            return std::nullopt;
        }
        value = value * base + digit;
    }
    return value;
}

inline bool unescapeXml(std::string_view raw, std::string &out) {
    out.clear();
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '&') {
            out += raw[i];
            continue;
        }
        const auto end = raw.find(';', i);
        if (end == std::string_view::npos) {
            return false;
        }
        const std::string_view entity = raw.substr(i + 1, end - i - 1);
        if (entity == "amp") {
            out += '&';
        } else if (entity == "lt") {
            out += '<';
        } else if (entity == "gt") {
            out += '>';
        } else if (entity == "quot") {
            out += '"';
        } else if (entity == "apos") {
            out += '\'';
        } else if (entity.size() > 1 && entity[0] == '#') {
            const bool hex = entity[1] == 'x';
            const auto value = parseNumber(entity.substr(hex ? 2 : 1), hex ? 16 : 10);
            if (!value || *value == 0 || !appendUtf8(out, *value)) {
                return false;
            }
        } else {
            return false;
        }
        i = end;
    }
    return true;
}
}
