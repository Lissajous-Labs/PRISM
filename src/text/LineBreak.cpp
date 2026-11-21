#include "text/LineBreak.h"

#include "text/UnicodeClasses.h"

namespace prism::text {
void findBreaks(std::span<const char32_t> text, std::vector<BreakAction> &breaks) {
    breaks.assign(text.size() + 1, BreakAction::none);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const BreakClass current = breakClass(text[i]);
        const BreakClass next = i + 1 < text.size() ? breakClass(text[i + 1]) : BreakClass::other;
        if (current == BreakClass::mandatory) {
            const bool crlf = text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n';
            breaks[i + 1] = crlf ? BreakAction::none : BreakAction::mandatory;
            continue;
        }
        if (i + 1 >= text.size()) {
            continue;
        }
        if (next == BreakClass::combining || next == BreakClass::closePunctuation ||
            next == BreakClass::space || next == BreakClass::mandatory ||
            current == BreakClass::openPunctuation) {
            continue;
        }
        if (current == BreakClass::space || current == BreakClass::hyphen ||
            current == BreakClass::ideographic || next == BreakClass::ideographic) {
            breaks[i + 1] = BreakAction::allowed;
        }
    }
    breaks.back() = BreakAction::mandatory;
}
}
