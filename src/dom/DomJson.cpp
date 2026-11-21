#include "dom/DomCodec.h"
#include "dom/TextEscapes.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace prism::dom {
namespace {
struct JsonValue {
    enum class Kind : std::uint8_t {
        null,
        boolean,
        number,
        string,
        array,
        object
    } kind = Kind::null;
    bool flag = false;
    std::string text;
    std::vector<JsonValue> items;
    std::vector<std::pair<std::string, JsonValue>> members;

    [[nodiscard]] const JsonValue *member(std::string_view key) const {
        for (const auto &[name, value] : members) {
            if (name == key) {
                return &value;
            }
        }
        return nullptr;
    }
};

class JsonParser {
  public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    std::optional<JsonValue> run() {
        JsonValue value;
        skipSpace();
        if (!parse(value, 0)) {
            return std::nullopt;
        }
        skipSpace();
        if (pos_ != text_.size()) {
            return std::nullopt;
        }
        return value;
    }

  private:
    std::string_view text_;
    std::size_t pos_ = 0;

    void skipSpace() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                       text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) != word) {
            return false;
        }
        pos_ += word.size();
        return true;
    }

    std::optional<std::uint32_t> hex4() {
        if (text_.size() - pos_ < 4) {
            return std::nullopt;
        }
        const auto value = parseNumber(text_.substr(pos_, 4), 16);
        pos_ += 4;
        return value;
    }

    bool string(std::string &out) {
        if (pos_ >= text_.size() || text_[pos_] != '"') {
            return false;
        }
        ++pos_;
        out.clear();
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return false;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) {
                return false;
            }
            const char escape = text_[pos_++];
            switch (escape) {
            case '"':
            case '\\':
            case '/':
                out += escape;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                auto unit = hex4();
                if (!unit) {
                    return false;
                }
                std::uint32_t codepoint = *unit;
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                    if (!literal("\\u")) {
                        return false;
                    }
                    const auto low = hex4();
                    if (!low || *low < 0xDC00 || *low > 0xDFFF) {
                        return false;
                    }
                    codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (*low - 0xDC00);
                }
                if (!appendUtf8(out, codepoint)) {
                    return false;
                }
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool number(JsonValue &value) {
        const std::size_t start = pos_;
        const auto digits = [&] {
            const std::size_t first = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
            return pos_ - first;
        };
        if (pos_ < text_.size() && text_[pos_] == '-') {
            ++pos_;
        }
        const std::size_t integerStart = pos_;
        const std::size_t integerDigits = digits();
        if (integerDigits == 0 || (integerDigits > 1 && text_[integerStart] == '0')) {
            return false;
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            if (digits() == 0) {
                return false;
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            if (digits() == 0) {
                return false;
            }
        }
        value.kind = JsonValue::Kind::number;
        value.text = text_.substr(start, pos_ - start);
        return true;
    }

    bool parse(JsonValue &value, std::size_t depth) {
        if (depth > maxDocumentDepth * 2 || pos_ >= text_.size()) {
            return false;
        }
        const char c = text_[pos_];
        if (c == '{') {
            ++pos_;
            value.kind = JsonValue::Kind::object;
            skipSpace();
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            for (;;) {
                skipSpace();
                std::string key;
                if (!string(key)) {
                    return false;
                }
                if (value.member(key) != nullptr) {
                    return false;
                }
                skipSpace();
                if (!literal(":")) {
                    return false;
                }
                skipSpace();
                JsonValue member;
                if (!parse(member, depth + 1)) {
                    return false;
                }
                value.members.emplace_back(std::move(key), std::move(member));
                skipSpace();
                if (literal("}")) {
                    return true;
                }
                if (!literal(",")) {
                    return false;
                }
            }
        }
        if (c == '[') {
            ++pos_;
            value.kind = JsonValue::Kind::array;
            skipSpace();
            if (literal("]")) {
                return true;
            }
            for (;;) {
                skipSpace();
                JsonValue item;
                if (!parse(item, depth + 1)) {
                    return false;
                }
                value.items.push_back(std::move(item));
                skipSpace();
                if (literal("]")) {
                    return true;
                }
                if (!literal(",")) {
                    return false;
                }
            }
        }
        if (c == '"') {
            value.kind = JsonValue::Kind::string;
            return string(value.text);
        }
        if (literal("true")) {
            value.kind = JsonValue::Kind::boolean;
            value.flag = true;
            return true;
        }
        if (literal("false")) {
            value.kind = JsonValue::Kind::boolean;
            return true;
        }
        if (literal("null")) {
            return true;
        }
        return number(value);
    }
};

bool buildNode(const JsonValue &value, DomDocument &document, NodeId parent, std::size_t depth) {
    if (depth >= maxDocumentDepth || value.kind != JsonValue::Kind::object) {
        return false;
    }
    const JsonValue *type = value.member("type");
    if (type == nullptr || type->kind != JsonValue::Kind::string || type->text.empty()) {
        return false;
    }
    const NodeId id = document.create(type->text, parent);
    if (!id.valid()) {
        return false;
    }
    const auto strings = [](const JsonValue *list, auto &&visitor) {
        if (list == nullptr) {
            return true;
        }
        if (list->kind != JsonValue::Kind::array) {
            return false;
        }
        for (const JsonValue &item : list->items) {
            if (item.kind != JsonValue::Kind::string || !visitor(item.text)) {
                return false;
            }
        }
        return true;
    };
    if (const JsonValue *element = value.member("id")) {
        if (element->kind != JsonValue::Kind::string || !document.setElementId(id, element->text)) {
            return false;
        }
    }
    if (!strings(value.member("classes"),
                 [&](const std::string &name) { return document.addClass(id, name); })) {
        return false;
    }
    std::uint8_t mask = 0;
    if (!strings(value.member("state"), [&](const std::string &name) {
            for (std::size_t bit = 0; bit < pseudo::names.size(); ++bit) {
                if (pseudo::names[bit] == name) {
                    mask |= static_cast<std::uint8_t>(1U << bit);
                    return true;
                }
            }
            return false;
        })) {
        return false;
    }
    document.setPseudo(id, mask);
    if (const JsonValue *attributes = value.member("attributes")) {
        if (attributes->kind != JsonValue::Kind::object) {
            return false;
        }
        for (const auto &[name, text] : attributes->members) {
            if (text.kind != JsonValue::Kind::string ||
                !document.setAttribute(id, name, text.text)) {
                return false;
            }
        }
    }
    if (const JsonValue *bindings = value.member("bindings")) {
        if (bindings->kind != JsonValue::Kind::array) {
            return false;
        }
        for (const JsonValue &binding : bindings->items) {
            const JsonValue *kind = binding.member("kind");
            const JsonValue *source = binding.member("source");
            const JsonValue *path = binding.member("path");
            if (kind == nullptr || source == nullptr || path == nullptr ||
                kind->kind != JsonValue::Kind::string || source->kind != JsonValue::Kind::string ||
                path->kind != JsonValue::Kind::string) {
                return false;
            }
            bool added = false;
            for (std::size_t k = 0; k < bindingNames.size(); ++k) {
                if (bindingNames[k] == kind->text) {
                    added = document.addBinding(id, static_cast<BindingKind>(k), source->text,
                                                path->text);
                }
            }
            if (!added) {
                return false;
            }
        }
    }
    if (const JsonValue *children = value.member("children")) {
        if (children->kind != JsonValue::Kind::array) {
            return false;
        }
        for (const JsonValue &child : children->items) {
            if (!buildNode(child, document, id, depth + 1)) {
                return false;
            }
        }
    }
    return true;
}

void writeString(std::string &out, std::string_view text) {
    constexpr char digits[] = "0123456789abcdef";
    out += '"';
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (byte < 0x20) {
            out += "\\u00";
            out += digits[byte >> 4];
            out += digits[byte & 15];
        } else {
            out += c;
        }
    }
    out += '"';
}

void writeNode(const DomDocument &document, NodeId id, std::string &out) {
    const DomNode &node = document.node(id);
    out += "{\"type\":";
    writeString(out, document.typeName(node));
    if (node.element != noString) {
        out += ",\"id\":";
        writeString(out, document.text(node.element));
    }
    const auto list = [&](std::string_view key, auto &&values) {
        out += ",\"";
        out += key;
        out += "\":[";
        bool first = true;
        for (const std::string_view value : values) {
            out += first ? "" : ",";
            first = false;
            writeString(out, value);
        }
        out += ']';
    };
    if (!node.classes.empty()) {
        std::vector<std::string_view> names;
        for (const StringId name : node.classes) {
            names.push_back(document.text(name));
        }
        list("classes", names);
    }
    if (node.pseudo != 0) {
        std::vector<std::string_view> names;
        for (std::size_t bit = 0; bit < pseudo::names.size(); ++bit) {
            if ((node.pseudo & (1U << bit)) != 0) {
                names.push_back(pseudo::names[bit]);
            }
        }
        list("state", names);
    }
    if (!node.attributes.empty()) {
        out += ",\"attributes\":{";
        bool first = true;
        for (const Attribute &attribute : node.attributes) {
            out += first ? "" : ",";
            first = false;
            writeString(out, document.text(attribute.name));
            out += ':';
            writeString(out, document.text(attribute.value));
        }
        out += '}';
    }
    if (!node.bindings.empty()) {
        out += ",\"bindings\":[";
        bool first = true;
        for (const Binding &binding : node.bindings) {
            out += first ? "{\"kind\":" : ",{\"kind\":";
            first = false;
            writeString(out, bindingNames[static_cast<std::size_t>(binding.kind)]);
            out += ",\"source\":";
            writeString(out, document.text(binding.source));
            out += ",\"path\":";
            writeString(out, document.text(binding.path));
            out += '}';
        }
        out += ']';
    }
    if (node.firstChild != noIndex) {
        out += ",\"children\":[";
        bool first = true;
        document.forEachChild(id, [&](NodeId child) {
            out += first ? "" : ",";
            first = false;
            writeNode(document, child, out);
        });
        out += ']';
    }
    out += '}';
}
}

std::string encodeJson(const DomDocument &document) {
    std::string out;
    if (document.root().valid()) {
        writeNode(document, document.root(), out);
    } else {
        out = "null";
    }
    return out;
}

std::optional<DomDocument> decodeJson(std::string_view text) {
    const auto value = JsonParser(text).run();
    if (!value) {
        return std::nullopt;
    }
    DomDocument document;
    if (value->kind == JsonValue::Kind::null) {
        return document;
    }
    if (!buildNode(*value, document, {}, 0)) {
        return std::nullopt;
    }
    return document;
}
}
