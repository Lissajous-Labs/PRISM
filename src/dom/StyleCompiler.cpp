#include "dom/StyleSheet.h"

#include <charconv>
#include <cmath>

namespace prism::dom {
namespace {
bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

bool identChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '_';
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<double> parseDouble(std::string_view text) {
    std::size_t i = 0;
    const bool negative = i < text.size() && text[i] == '-';
    i += i < text.size() && (text[i] == '-' || text[i] == '+') ? 1 : 0;
    double mantissa = 0;
    int exponent = 0;
    std::size_t digits = 0;
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i, ++digits) {
        mantissa = mantissa * 10 + (text[i] - '0');
    }
    if (i < text.size() && text[i] == '.') {
        for (++i; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i, ++digits) {
            mantissa = mantissa * 10 + (text[i] - '0');
            --exponent;
        }
    }
    if (digits == 0) {
        return std::nullopt;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        const bool negativeExponent = i < text.size() && text[i] == '-';
        i += i < text.size() && (text[i] == '-' || text[i] == '+') ? 1 : 0;
        int power = 0;
        std::size_t powerDigits = 0;
        for (; i < text.size() && text[i] >= '0' && text[i] <= '9' && power < 10000;
             ++i, ++powerDigits) {
            power = power * 10 + (text[i] - '0');
        }
        if (powerDigits == 0) {
            return std::nullopt;
        }
        exponent += negativeExponent ? -power : power;
    }
    if (i != text.size()) {
        return std::nullopt;
    }
    const double value =
        (exponent < 0 ? mantissa / std::pow(10.0, -exponent) : mantissa * std::pow(10.0, exponent));
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    return negative ? -value : value;
}

ValueKind kindOf(Property property) {
    switch (property) {
    case Property::color:
    case Property::background:
    case Property::borderColor:
        return ValueKind::color;
    case Property::opacity:
    case Property::scale:
    case Property::rotate:
    case Property::flexGrow:
    case Property::flexShrink:
    case Property::order:
        return ValueKind::number;
    case Property::display:
    case Property::flexDirection:
    case Property::flexWrap:
    case Property::justifyContent:
    case Property::alignItems:
    case Property::alignSelf:
    case Property::visibility:
        return ValueKind::keyword;
    case Property::transition:
        return ValueKind::transition;
    default:
        return ValueKind::length;
    }
}

std::optional<std::uint32_t> parseColor(std::string_view text) {
    if (text == "transparent") {
        return 0;
    }
    if (text.size() < 2 || text[0] != '#') {
        return std::nullopt;
    }
    std::string_view digits = text.substr(1);
    std::string expanded;
    if (digits.size() == 3 || digits.size() == 4) {
        for (const char c : digits) {
            expanded += c;
            expanded += c;
        }
        digits = expanded;
    }
    if (digits.size() != 6 && digits.size() != 8) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return digits.size() == 6 ? (value << 8) | 0xFF : value;
}

std::optional<Length> parseLength(std::string_view text) {
    if (text == "auto") {
        return Length{0, Unit::automatic, {}};
    }
    std::size_t split = text.size();
    while (split > 0 && !(text[split - 1] >= '0' && text[split - 1] <= '9') &&
           text[split - 1] != '.') {
        --split;
    }
    const auto number = parseDouble(text.substr(0, split));
    if (!number) {
        return std::nullopt;
    }
    const std::string_view suffix = text.substr(split);
    if (suffix.empty()) {
        if (*number != 0) {
            return std::nullopt;
        }
        return Length{0, Unit::px, {}};
    }
    const auto unit = unitTable.find(suffix);
    if (!unit) {
        return std::nullopt;
    }
    return Length{static_cast<float>(*number), static_cast<Unit>(*unit + 1), {}};
}

std::optional<StyleValue> genericValue(std::string_view text) {
    StyleValue value;
    if (const auto color = parseColor(text); color && text.front() == '#') {
        value.kind = ValueKind::color;
        value.color = *color;
        return value;
    }
    if (const auto number = parseDouble(text)) {
        value.kind = ValueKind::number;
        value.number = static_cast<float>(*number);
        return value;
    }
    if (const auto length = parseLength(text)) {
        value.kind = ValueKind::length;
        value.length = *length;
        return value;
    }
    if (const auto keyword = keywordTable.find(text)) {
        value.kind = ValueKind::keyword;
        value.keyword = static_cast<Keyword>(*keyword);
        return value;
    }
    return std::nullopt;
}

class Compiler {
  public:
    explicit Compiler(std::string_view source) : source_(source) {}

    CompileResult run() {
        while (skipTrivia(), pos_ < source_.size()) {
            if (source_.substr(pos_, 6) == "@media") {
                if (!media()) {
                    return fail();
                }
            } else if (!ruleSet(0)) {
                return fail();
            }
        }
        return {std::move(sheet_), {}};
    }

  private:
    std::string_view source_;
    std::size_t pos_ = 0;
    StyleSheet sheet_;
    std::string message_ = "unexpected input";

    CompileResult fail() {
        std::uint32_t line = 1;
        for (std::size_t i = 0; i < pos_ && i < source_.size(); ++i) {
            line += source_[i] == '\n' ? 1 : 0;
        }
        return {std::nullopt, {line, message_}};
    }

    bool error(std::string message) {
        message_ = std::move(message);
        return false;
    }

    void skipTrivia() {
        for (;;) {
            while (pos_ < source_.size() && space(source_[pos_])) {
                ++pos_;
            }
            if (source_.substr(pos_, 2) != "/*") {
                return;
            }
            const auto end = source_.find("*/", pos_ + 2);
            pos_ = end == std::string_view::npos ? source_.size() : end + 2;
        }
    }

    std::uint32_t line() const {
        std::uint32_t count = 1;
        for (std::size_t i = 0; i < pos_; ++i) {
            count += source_[i] == '\n' ? 1 : 0;
        }
        return count;
    }

    bool media() {
        pos_ += 6;
        const auto open = source_.find('{', pos_);
        if (open == std::string_view::npos) {
            return error("media block without body");
        }
        MediaRange range;
        std::string_view conditions = source_.substr(pos_, open - pos_);
        while (!(conditions = trim(conditions)).empty()) {
            if (conditions.substr(0, 3) == "and") {
                conditions.remove_prefix(3);
                continue;
            }
            if (conditions.front() != '(') {
                return error("malformed media condition");
            }
            const auto close = conditions.find(')');
            const auto colon = conditions.find(':');
            if (close == std::string_view::npos || colon == std::string_view::npos ||
                colon > close) {
                return error("malformed media condition");
            }
            const std::string_view feature = trim(conditions.substr(1, colon - 1));
            const auto length = parseLength(trim(conditions.substr(colon + 1, close - colon - 1)));
            if (!length || length->unit != Unit::px) {
                return error("media conditions require pixel lengths");
            }
            if (feature == "min-width") {
                range.minWidth = length->value;
            } else if (feature == "max-width") {
                range.maxWidth = length->value;
            } else if (feature == "min-height") {
                range.minHeight = length->value;
            } else if (feature == "max-height") {
                range.maxHeight = length->value;
            } else {
                return error("unknown media feature");
            }
            conditions.remove_prefix(close + 1);
        }
        sheet_.media.push_back(range);
        const auto index = static_cast<std::uint32_t>(sheet_.media.size() - 1);
        pos_ = open + 1;
        while (skipTrivia(), pos_ < source_.size() && source_[pos_] != '}') {
            if (!ruleSet(index)) {
                return false;
            }
        }
        if (pos_ >= source_.size()) {
            return error("unterminated media block");
        }
        ++pos_;
        return true;
    }

    bool compound(std::string_view text, Compound &out) {
        std::size_t i = 0;
        const auto ident = [&] {
            const std::size_t start = i;
            while (i < text.size() && identChar(text[i])) {
                ++i;
            }
            return text.substr(start, i - start);
        };
        if (i < text.size() && text[i] == '*') {
            ++i;
        } else if (i < text.size() && identChar(text[i])) {
            out.type = tokenHash(TokenKind::type, ident());
        }
        while (i < text.size()) {
            const char c = text[i++];
            if (c == '#' || c == '.') {
                const std::string_view name = ident();
                if (name.empty()) {
                    return error("empty selector name");
                }
                if (c == '#') {
                    out.element = tokenHash(TokenKind::element, name);
                } else if (!out.classes.push_back(tokenHash(TokenKind::klass, name))) {
                    return error("too many classes in compound selector");
                }
            } else if (c == ':') {
                const std::string_view name = ident();
                if (name == "root") {
                    out.root = true;
                } else if (const auto bit = pseudoTable.find(name)) {
                    out.pseudo |= static_cast<std::uint8_t>(1U << *bit);
                } else {
                    return error("unknown pseudo-class");
                }
            } else if (c == '[') {
                const auto close = text.find(']', i);
                if (close == std::string_view::npos ||
                    !predicate(trim(text.substr(i, close - i)), out)) {
                    return error("malformed predicate");
                }
                i = close + 1;
            } else {
                return error("unexpected selector character");
            }
        }
        return true;
    }

    bool predicate(std::string_view text, Compound &out) {
        static constexpr std::array<std::pair<std::string_view, PredicateOp>, 7> operators{
            {{"==", PredicateOp::eq},
             {"!=", PredicateOp::ne},
             {"<=", PredicateOp::le},
             {">=", PredicateOp::ge},
             {"<", PredicateOp::lt},
             {">", PredicateOp::gt},
             {"=", PredicateOp::attributeEquals}}};
        for (const auto &[symbol, op] : operators) {
            const auto at = text.find(symbol);
            if (at == std::string_view::npos) {
                continue;
            }
            const std::string_view name = trim(text.substr(0, at));
            std::string_view value = trim(text.substr(at + symbol.size()));
            if (name.empty() || value.empty()) {
                return false;
            }
            Predicate result{op, sheet_.intern(name), 0, 0};
            if (op == PredicateOp::attributeEquals) {
                if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
                    value.back() == value.front()) {
                    value = value.substr(1, value.size() - 2);
                }
                result.text = sheet_.intern(value);
            } else if (value == "true" || value == "false") {
                result.number = value == "true" ? 1 : 0;
            } else if (const auto number = parseDouble(value)) {
                result.number = *number;
            } else {
                return false;
            }
            return out.predicates.push_back(result);
        }
        if (text.empty()) {
            return false;
        }
        return out.predicates.push_back({PredicateOp::exists, sheet_.intern(text), 0, 0});
    }

    bool selector(std::string_view text, StyleRule &rule) {
        std::vector<Compound> leftToRight;
        std::vector<Combinator> combinators;
        std::size_t i = 0;
        Combinator pending = Combinator::none;
        while (i < text.size()) {
            while (i < text.size() && space(text[i])) {
                pending = pending == Combinator::none && !leftToRight.empty()
                              ? Combinator::descendant
                              : pending;
                ++i;
            }
            if (i < text.size() && text[i] == '>') {
                pending = Combinator::child;
                ++i;
                continue;
            }
            if (i >= text.size()) {
                break;
            }
            std::size_t end = i;
            int depth = 0;
            while (end < text.size() && (depth > 0 || (!space(text[end]) && text[end] != '>'))) {
                depth += text[end] == '[' ? 1 : (text[end] == ']' ? -1 : 0);
                ++end;
            }
            Compound part;
            if (!compound(text.substr(i, end - i), part)) {
                return false;
            }
            if (!leftToRight.empty()) {
                if (pending == Combinator::none) {
                    return error("missing combinator");
                }
                combinators.push_back(pending);
            }
            leftToRight.push_back(part);
            pending = Combinator::none;
            i = end;
        }
        if (leftToRight.empty() || pending == Combinator::child) {
            return error("empty selector");
        }
        std::uint32_t ids = 0;
        std::uint32_t classes = 0;
        std::uint32_t types = 0;
        for (std::size_t k = leftToRight.size(); k-- > 0;) {
            Compound &part = leftToRight[k];
            part.toLeft = k > 0 ? combinators[k - 1] : Combinator::none;
            ids += part.element != 0 ? 1 : 0;
            classes += static_cast<std::uint32_t>(
                part.classes.size() + part.predicates.size() +
                static_cast<std::size_t>(std::popcount(part.pseudo)) + (part.root ? 1 : 0));
            types += part.type != 0 ? 1 : 0;
            if (k + 1 < leftToRight.size()) {
                if (part.type != 0) {
                    rule.ancestorMask.add(part.type);
                }
                if (part.element != 0) {
                    rule.ancestorMask.add(part.element);
                }
                for (const std::uint64_t name : part.classes) {
                    rule.ancestorMask.add(name);
                }
            }
            rule.compounds.push_back(part);
        }
        rule.specificity = Specificity::of(false, ids, classes, types,
                                           static_cast<std::uint32_t>(sheet_.rules.size()));
        return true;
    }

    bool ruleSet(std::uint32_t media) {
        const std::uint32_t ruleLine = line();
        const auto open = source_.find('{', pos_);
        const auto close = open == std::string_view::npos ? open : source_.find('}', open);
        if (open == std::string_view::npos || close == std::string_view::npos) {
            return error("rule without body");
        }
        const std::string_view selectors = source_.substr(pos_, open - pos_);
        const std::string_view body = source_.substr(open + 1, close - open - 1);
        std::vector<Declaration> declarations;
        const bool rootTokens = trim(selectors) == ":root";
        if (rootTokens) {
            if (!tokens(body)) {
                return error("malformed design token");
            }
        }
        if (!parseDeclarations(body, sheet_, declarations, !rootTokens)) {
            return error("malformed declaration");
        }
        std::size_t start = 0;
        while (start <= selectors.size()) {
            const auto comma = selectors.find(',', start);
            const std::string_view one = trim(selectors.substr(
                start, comma == std::string_view::npos ? std::string_view::npos : comma - start));
            StyleRule rule;
            if (!selector(one, rule)) {
                return false;
            }
            rule.firstDeclaration = static_cast<std::uint32_t>(sheet_.declarations.size());
            rule.declarationCount = static_cast<std::uint32_t>(declarations.size());
            rule.media = media;
            rule.line = ruleLine;
            sheet_.declarations.insert(sheet_.declarations.end(), declarations.begin(),
                                       declarations.end());
            sheet_.rules.push_back(std::move(rule));
            if (comma == std::string_view::npos) {
                break;
            }
            start = comma + 1;
        }
        pos_ = close + 1;
        return true;
    }

    bool tokens(std::string_view body) {
        std::size_t start = 0;
        while (start < body.size()) {
            auto end = body.find(';', start);
            end = end == std::string_view::npos ? body.size() : end;
            const std::string_view item = trim(body.substr(start, end - start));
            start = end + 1;
            if (item.substr(0, 2) != "--") {
                continue;
            }
            const auto colon = item.find(':');
            if (colon == std::string_view::npos) {
                return false;
            }
            const auto value = genericValue(trim(item.substr(colon + 1)));
            if (!value || !sheet_.setToken(trim(item.substr(0, colon)), *value)) {
                return false;
            }
        }
        return true;
    }

  public:
    static bool parseDeclarations(std::string_view body, StyleSheet &sheet,
                                  std::vector<Declaration> &out, bool rejectTokens);
};

bool Compiler::parseDeclarations(std::string_view body, StyleSheet &sheet,
                                 std::vector<Declaration> &out, bool rejectTokens) {
    std::size_t start = 0;
    while (start < body.size()) {
        auto end = body.find(';', start);
        end = end == std::string_view::npos ? body.size() : end;
        const std::string_view item = trim(body.substr(start, end - start));
        start = end + 1;
        if (item.empty()) {
            continue;
        }
        const auto colon = item.find(':');
        if (colon == std::string_view::npos) {
            return false;
        }
        const std::string_view name = trim(item.substr(0, colon));
        if (name.substr(0, 2) == "--") {
            if (rejectTokens) {
                return false;
            }
            continue;
        }
        const auto property = propertyTable.find(name);
        if (!property) {
            return false;
        }
        const auto id = static_cast<Property>(*property);
        std::string_view values = trim(item.substr(colon + 1));
        while (!values.empty()) {
            const auto comma =
                id == Property::transition ? values.find(',') : std::string_view::npos;
            const auto value = parseValue(id, trim(values.substr(0, comma)), sheet);
            if (!value) {
                return false;
            }
            out.push_back({id, *value});
            values = comma == std::string_view::npos ? std::string_view{}
                                                     : trim(values.substr(comma + 1));
        }
    }
    return true;
}
}

std::uint32_t StyleSheet::intern(std::string_view text) {
    for (std::size_t i = 0; i < strings.size(); ++i) {
        if (strings[i] == text) {
            return static_cast<std::uint32_t>(i);
        }
    }
    strings.emplace_back(text);
    return static_cast<std::uint32_t>(strings.size() - 1);
}

std::optional<std::uint32_t> StyleSheet::token(std::string_view name) const {
    const std::uint64_t hash = math::hash64(name);
    for (std::size_t i = 0; i < tokenNames.size(); ++i) {
        if (tokenNames[i] == hash) {
            return static_cast<std::uint32_t>(i);
        }
    }
    return std::nullopt;
}

bool StyleSheet::setToken(std::string_view name, const StyleValue &value) {
    if (value.kind == ValueKind::token) {
        return false;
    }
    if (const auto existing = token(name)) {
        tokenValues[*existing] = value;
        return true;
    }
    tokenNames.push_back(math::hash64(name));
    tokenValues.push_back(value);
    return true;
}

std::optional<StyleValue> parseValue(Property property, std::string_view text, StyleSheet &sheet) {
    StyleValue value;
    if (text.substr(0, 4) == "var(" && text.back() == ')') {
        const std::string_view name = trim(text.substr(4, text.size() - 5));
        if (name.substr(0, 2) != "--") {
            return std::nullopt;
        }
        auto index = sheet.token(name);
        if (!index) {
            sheet.tokenNames.push_back(math::hash64(name));
            sheet.tokenValues.push_back({});
            index = static_cast<std::uint32_t>(sheet.tokenNames.size() - 1);
        }
        value.kind = ValueKind::token;
        value.token = *index;
        return value;
    }
    value.kind = kindOf(property);
    switch (value.kind) {
    case ValueKind::color: {
        const auto color = parseColor(text);
        if (!color) {
            return std::nullopt;
        }
        value.color = *color;
        return value;
    }
    case ValueKind::number: {
        std::string_view digits = text;
        if (property == Property::rotate && digits.size() > 3 &&
            digits.substr(digits.size() - 3) == "deg") {
            digits.remove_suffix(3);
        }
        const auto number = parseDouble(digits);
        if (!number) {
            return std::nullopt;
        }
        value.number = static_cast<float>(*number);
        return value;
    }
    case ValueKind::keyword: {
        const auto keyword = keywordTable.find(text);
        if (!keyword) {
            return std::nullopt;
        }
        value.keyword = static_cast<Keyword>(*keyword);
        return value;
    }
    case ValueKind::transition: {
        const auto open = text.find("spring(");
        if (open == std::string_view::npos || text.back() != ')') {
            return std::nullopt;
        }
        const auto target = propertyTable.find(trim(text.substr(0, open)));
        const auto omega = parseDouble(trim(text.substr(open + 7, text.size() - open - 8)));
        if (!target || !omega || *omega <= 0) {
            return std::nullopt;
        }
        value.transitionProperty = static_cast<Property>(*target);
        value.number = static_cast<float>(*omega);
        return value;
    }
    default: {
        const auto length = parseLength(text);
        if (!length) {
            return std::nullopt;
        }
        value.length = *length;
        return value;
    }
    }
}

bool parseDeclarations(std::string_view source, StyleSheet &sheet, std::vector<Declaration> &out) {
    return Compiler::parseDeclarations(source, sheet, out, true);
}

ValueKind valueKindOf(Property property) noexcept { return kindOf(property); }

CompileResult compileStyleSheet(std::string_view source) { return Compiler(source).run(); }
}
