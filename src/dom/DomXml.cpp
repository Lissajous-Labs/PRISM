#include "dom/DomCodec.h"
#include "dom/TextEscapes.h"

#include <string>

namespace prism::dom {
namespace {
void escapeAttribute(std::string &out, std::string_view text) {
    for (const char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\n':
            out += "&#10;";
            break;
        case '\t':
            out += "&#9;";
            break;
        case '\r':
            out += "&#13;";
            break;
        default:
            out += c;
        }
    }
}

void writeAttribute(std::string &out, std::string_view name, std::string_view value) {
    out += ' ';
    out += name;
    out += "=\"";
    escapeAttribute(out, value);
    out += '"';
}

void writeNode(const DomDocument &document, NodeId id, std::size_t depth, std::string &out) {
    const DomNode &node = document.node(id);
    out.append(depth * 2, ' ');
    out += '<';
    out += document.typeName(node);
    if (node.element != noString) {
        writeAttribute(out, "id", document.text(node.element));
    }
    if (!node.classes.empty()) {
        std::string joined;
        for (const StringId name : node.classes) {
            joined += joined.empty() ? "" : " ";
            joined += document.text(name);
        }
        writeAttribute(out, "class", joined);
    }
    if (node.pseudo != 0) {
        std::string joined;
        for (std::size_t bit = 0; bit < pseudo::names.size(); ++bit) {
            if ((node.pseudo & (1U << bit)) != 0) {
                joined += joined.empty() ? "" : " ";
                joined += pseudo::names[bit];
            }
        }
        writeAttribute(out, "state", joined);
    }
    if (!node.bindings.empty()) {
        std::string joined;
        for (const Binding &binding : node.bindings) {
            joined += joined.empty() ? "" : " ";
            joined += bindingNames[static_cast<std::size_t>(binding.kind)];
            joined += ':';
            joined += document.text(binding.source);
            joined += '@';
            joined += document.text(binding.path);
        }
        writeAttribute(out, "bind", joined);
    }
    for (const Attribute &attribute : node.attributes) {
        writeAttribute(out, document.text(attribute.name), document.text(attribute.value));
    }
    if (node.firstChild == noIndex) {
        out += "/>\n";
        return;
    }
    out += ">\n";
    document.forEachChild(id, [&](NodeId child) { writeNode(document, child, depth + 1, out); });
    out.append(depth * 2, ' ');
    out += "</";
    out += document.typeName(node);
    out += ">\n";
}

bool nameStart(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':';
}

bool nameChar(char c) { return nameStart(c) || (c >= '0' && c <= '9') || c == '-' || c == '.'; }

bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

template <class Visitor> bool splitWords(std::string_view text, Visitor &&visitor) {
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && space(text[i])) {
            ++i;
        }
        const std::size_t start = i;
        while (i < text.size() && !space(text[i])) {
            ++i;
        }
        if (i > start && !visitor(text.substr(start, i - start))) {
            return false;
        }
    }
    return true;
}

class XmlParser {
  public:
    explicit XmlParser(std::string_view text) : text_(text) {}

    std::optional<DomDocument> run() {
        skipMisc();
        if (!element({}, 0)) {
            return std::nullopt;
        }
        skipMisc();
        if (failed_ || pos_ != text_.size()) {
            return std::nullopt;
        }
        return std::move(document_);
    }

  private:
    std::string_view text_;
    std::size_t pos_ = 0;
    bool failed_ = false;
    DomDocument document_;
    std::string scratch_;

    bool startsWith(std::string_view token) const {
        return text_.substr(pos_, token.size()) == token;
    }

    void skipSpace() {
        while (pos_ < text_.size() && space(text_[pos_])) {
            ++pos_;
        }
    }

    void skipMisc() {
        for (;;) {
            skipSpace();
            if (startsWith("<?")) {
                const auto end = text_.find("?>", pos_ + 2);
                if (end == std::string_view::npos) {
                    failed_ = true;
                    return;
                }
                pos_ = end + 2;
            } else if (startsWith("<!--")) {
                const auto end = text_.find("-->", pos_ + 4);
                if (end == std::string_view::npos) {
                    failed_ = true;
                    return;
                }
                pos_ = end + 3;
            } else {
                return;
            }
        }
    }

    std::optional<std::string_view> name() {
        if (pos_ >= text_.size() || !nameStart(text_[pos_])) {
            return std::nullopt;
        }
        const std::size_t start = pos_;
        while (pos_ < text_.size() && nameChar(text_[pos_])) {
            ++pos_;
        }
        return text_.substr(start, pos_ - start);
    }

    bool attributeValue(std::string &out) {
        if (pos_ >= text_.size() || (text_[pos_] != '"' && text_[pos_] != '\'')) {
            return false;
        }
        const char quote = text_[pos_++];
        const auto end = text_.find(quote, pos_);
        if (end == std::string_view::npos) {
            return false;
        }
        const std::string_view raw = text_.substr(pos_, end - pos_);
        pos_ = end + 1;
        return raw.find('<') == std::string_view::npos && unescapeXml(raw, out);
    }

    bool applyAttribute(NodeId id, std::string_view key, std::string_view value) {
        if (key == "id") {
            return document_.setElementId(id, value);
        }
        if (key == "class") {
            return splitWords(value,
                              [&](std::string_view word) { return document_.addClass(id, word); });
        }
        if (key == "state") {
            std::uint8_t mask = 0;
            const bool ok = splitWords(value, [&](std::string_view word) {
                for (std::size_t bit = 0; bit < pseudo::names.size(); ++bit) {
                    if (pseudo::names[bit] == word) {
                        mask |= static_cast<std::uint8_t>(1U << bit);
                        return true;
                    }
                }
                return false;
            });
            document_.setPseudo(id, mask);
            return ok;
        }
        if (key == "bind") {
            return splitWords(value, [&](std::string_view word) {
                const auto colon = word.find(':');
                const auto at = word.find('@');
                if (colon == std::string_view::npos || at == std::string_view::npos || at < colon) {
                    return false;
                }
                for (std::size_t kind = 0; kind < bindingNames.size(); ++kind) {
                    if (bindingNames[kind] == word.substr(0, colon)) {
                        return document_.addBinding(id, static_cast<BindingKind>(kind),
                                                    word.substr(colon + 1, at - colon - 1),
                                                    word.substr(at + 1));
                    }
                }
                return false;
            });
        }
        if (document_.attribute(id, key)) {
            return false;
        }
        return document_.setAttribute(id, key, value);
    }

    bool element(NodeId parent, std::size_t depth) {
        if (depth >= maxDocumentDepth || !startsWith("<")) {
            return false;
        }
        ++pos_;
        const auto type = name();
        if (!type) {
            return false;
        }
        const NodeId id = document_.create(*type, parent);
        if (!id.valid()) {
            return false;
        }
        for (;;) {
            const std::size_t before = pos_;
            skipSpace();
            if (startsWith("/>")) {
                pos_ += 2;
                return true;
            }
            if (startsWith(">")) {
                ++pos_;
                break;
            }
            if (pos_ == before) {
                return false;
            }
            const auto key = name();
            if (!key) {
                return false;
            }
            skipSpace();
            if (!startsWith("=")) {
                return false;
            }
            ++pos_;
            skipSpace();
            if (!attributeValue(scratch_) || !applyAttribute(id, *key, scratch_)) {
                return false;
            }
        }
        for (;;) {
            skipMisc();
            if (failed_ || pos_ >= text_.size()) {
                return false;
            }
            if (startsWith("</")) {
                pos_ += 2;
                const auto closing = name();
                skipSpace();
                if (!closing || *closing != *type || !startsWith(">")) {
                    return false;
                }
                ++pos_;
                return true;
            }
            if (!element(id, depth + 1)) {
                return false;
            }
        }
    }
};
}

std::string encodeXml(const DomDocument &document) {
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    if (document.root().valid()) {
        writeNode(document, document.root(), 0, out);
    }
    return out;
}

std::optional<DomDocument> decodeXml(std::string_view text) { return XmlParser(text).run(); }
}
