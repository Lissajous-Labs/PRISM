#include "text/UnicodeClasses.h"

#include <array>

namespace prism::text {
namespace {
struct BidiRange {
    char32_t first;
    char32_t last;
    BidiClass value;
};

constexpr std::array<BidiRange, 38> bidiRanges{{
    {0x0000, 0x0008, BidiClass::BN}, {0x0009, 0x0009, BidiClass::S},
    {0x000A, 0x000A, BidiClass::B},  {0x000B, 0x000B, BidiClass::S},
    {0x000C, 0x000C, BidiClass::WS}, {0x000D, 0x000D, BidiClass::B},
    {0x000E, 0x001B, BidiClass::BN}, {0x001C, 0x001E, BidiClass::B},
    {0x001F, 0x001F, BidiClass::S},  {0x0020, 0x0020, BidiClass::WS},
    {0x0021, 0x0022, BidiClass::ON}, {0x0023, 0x0025, BidiClass::ET},
    {0x0026, 0x002A, BidiClass::ON}, {0x002B, 0x002B, BidiClass::ES},
    {0x002C, 0x002C, BidiClass::CS}, {0x002D, 0x002D, BidiClass::ES},
    {0x002E, 0x002F, BidiClass::CS}, {0x0030, 0x0039, BidiClass::EN},
    {0x003A, 0x003A, BidiClass::CS}, {0x003B, 0x0040, BidiClass::ON},
    {0x005B, 0x0060, BidiClass::ON}, {0x007B, 0x007E, BidiClass::ON},
    {0x007F, 0x009F, BidiClass::BN}, {0x00A0, 0x00A0, BidiClass::CS},
    {0x00A2, 0x00A5, BidiClass::ET}, {0x0300, 0x036F, BidiClass::NSM},
    {0x0590, 0x05FF, BidiClass::R},  {0x0591, 0x05BD, BidiClass::NSM},
    {0x0600, 0x065F, BidiClass::AL}, {0x064B, 0x065F, BidiClass::NSM},
    {0x0660, 0x0669, BidiClass::AN}, {0x066A, 0x06EF, BidiClass::AL},
    {0x06F0, 0x06F9, BidiClass::EN}, {0x06FA, 0x08FF, BidiClass::AL},
    {0x2000, 0x200A, BidiClass::WS}, {0x2028, 0x2028, BidiClass::WS},
    {0xFB1D, 0xFB4F, BidiClass::R},  {0xFB50, 0xFEFF, BidiClass::AL},
}};
}

BidiClass bidiClass(char32_t codepoint) noexcept {
    BidiClass result = BidiClass::L;
    for (const BidiRange &range : bidiRanges) {
        if (codepoint >= range.first && codepoint <= range.last) {
            result = range.value;
        }
    }
    if (codepoint == 0x2029) {
        return BidiClass::B;
    }
    return result;
}

BreakClass breakClass(char32_t codepoint) noexcept {
    if (codepoint == '\n' || codepoint == '\r' || codepoint == 0x2028 || codepoint == 0x2029) {
        return BreakClass::mandatory;
    }
    if (codepoint == ' ' || codepoint == '\t' || codepoint == 0x3000) {
        return BreakClass::space;
    }
    if (codepoint == '-' || codepoint == 0x2010 || codepoint == 0x2013) {
        return BreakClass::hyphen;
    }
    if ((codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||
        (codepoint >= 0x3040 && codepoint <= 0x30FF) ||
        (codepoint >= 0xAC00 && codepoint <= 0xD7AF)) {
        return BreakClass::ideographic;
    }
    if (codepoint == '(' || codepoint == '[' || codepoint == '{') {
        return BreakClass::openPunctuation;
    }
    if (codepoint == ')' || codepoint == ']' || codepoint == '}' || codepoint == ',' ||
        codepoint == '.' || codepoint == '!' || codepoint == '?' || codepoint == ';' ||
        codepoint == ':') {
        return BreakClass::closePunctuation;
    }
    if (codepoint >= 0x0300 && codepoint <= 0x036F) {
        return BreakClass::combining;
    }
    return BreakClass::other;
}
}
