#include "text/UtfFastPath.h"

namespace prism::text {
void decodeUtf8(std::string_view text, std::vector<char32_t> &output) {
    output.clear();
    output.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t ascii = asciiPrefix(text.substr(i));
        for (std::size_t k = 0; k < ascii; ++k) {
            output.push_back(static_cast<unsigned char>(text[i + k]));
        }
        i += ascii;
        if (i >= text.size()) {
            break;
        }
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        char32_t value = 0;
        char32_t minimum = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
            value = lead & 0x1Fu;
            minimum = 0x80;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            length = 3;
            value = lead & 0x0Fu;
            minimum = 0x800;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            length = 4;
            value = lead & 0x07u;
            minimum = 0x10000;
        }
        std::size_t consumed = 1;
        bool valid = length != 0;
        while (valid && consumed < length) {
            if (i + consumed >= text.size()) {
                valid = false;
                break;
            }
            const auto byte = static_cast<unsigned char>(text[i + consumed]);
            unsigned char low = 0x80;
            unsigned char high = 0xBF;
            if (consumed == 1) {
                low = lead == 0xE0 ? 0xA0 : (lead == 0xF0 ? 0x90 : 0x80);
                high = lead == 0xED ? 0x9F : (lead == 0xF4 ? 0x8F : 0xBF);
            }
            if (byte < low || byte > high) {
                valid = false;
                break;
            }
            value = (value << 6) | (byte & 0x3Fu);
            ++consumed;
        }
        if (valid && value >= minimum) {
            output.push_back(value);
            i += length;
        } else {
            output.push_back(replacementCharacter);
            i += consumed;
        }
    }
}
}
