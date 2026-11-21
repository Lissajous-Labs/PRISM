#include "dom/DomCodec.h"
#include "dom/PrefixVarint.h"

#include <array>
#include <cstring>

namespace prism::dom {
namespace {
constexpr std::array<std::uint8_t, 4> magic{'P', 'R', 'B', 'N'};
constexpr std::size_t trailerSize = 4 + 8;

void encodeNode(const DomDocument &document, std::uint32_t index, std::vector<std::uint8_t> &out,
                std::vector<std::uint8_t> &record) {
    const NodeId id = document.handle(index);
    const DomNode &node = document.node(id);
    record.clear();
    writeVarint(record, *document.strings().find(document.typeName(node)));
    writeVarint(record, node.element);
    writeVarint(record, node.pseudo);
    writeVarint(record, node.classes.size());
    for (const StringId name : node.classes) {
        writeVarint(record, name);
    }
    writeVarint(record, node.attributes.size());
    for (const Attribute &attribute : node.attributes) {
        writeVarint(record, attribute.name);
        writeVarint(record, attribute.value);
    }
    writeVarint(record, node.bindings.size());
    for (const Binding &binding : node.bindings) {
        writeVarint(record, static_cast<std::uint64_t>(binding.kind));
        writeVarint(record, binding.source);
        writeVarint(record, binding.path);
    }
    std::uint64_t children = 0;
    for (std::uint32_t child = node.firstChild; child != noIndex;
         child = document.node(document.handle(child)).nextSibling) {
        ++children;
    }
    writeVarint(record, children);
    writeVarint(out, record.size());
    out.insert(out.end(), record.begin(), record.end());
    for (std::uint32_t child = node.firstChild; child != noIndex;
         child = document.node(document.handle(child)).nextSibling) {
        encodeNode(document, child, out, record);
    }
}

struct Frame {
    NodeId node;
    std::uint64_t remaining;
};

class Decoder {
  public:
    explicit Decoder(std::span<const std::uint8_t> body) : reader_(body) {}

    std::optional<DomDocument> run() {
        const auto header = reader_.bytes(magic.size());
        if (!header || std::memcmp(header->data(), magic.data(), magic.size()) != 0) {
            return std::nullopt;
        }
        const auto version = reader_.read();
        const auto stringCount = reader_.read();
        if (!version || *version == 0 || !stringCount || *stringCount == 0 ||
            *stringCount > reader_.size()) {
            return std::nullopt;
        }
        stringCount_ = *stringCount;
        for (std::uint64_t i = 1; i < stringCount_; ++i) {
            const auto length = reader_.read();
            if (!length) {
                return std::nullopt;
            }
            const auto bytes = reader_.bytes(*length);
            if (!bytes) {
                return std::nullopt;
            }
            const std::string_view text(reinterpret_cast<const char *>(bytes->data()),
                                        bytes->size());
            if (document_.strings().intern(text) != i) {
                return std::nullopt;
            }
        }
        const auto nodeCount = reader_.read();
        if (!nodeCount || *nodeCount > reader_.size()) {
            return std::nullopt;
        }
        std::vector<Frame> stack;
        for (std::uint64_t n = 0; n < *nodeCount; ++n) {
            while (!stack.empty() && stack.back().remaining == 0) {
                stack.pop_back();
            }
            if (n > 0 && stack.empty()) {
                return std::nullopt;
            }
            const NodeId parent = stack.empty() ? NodeId{} : stack.back().node;
            if (!stack.empty()) {
                --stack.back().remaining;
            }
            const auto children = readNode(parent);
            if (!children || stack.size() >= maxDocumentDepth) {
                return std::nullopt;
            }
            stack.push_back({lastNode_, *children});
        }
        for (const Frame &frame : stack) {
            if (frame.remaining != 0) {
                return std::nullopt;
            }
        }
        if (reader_.offset() != reader_.size()) {
            return std::nullopt;
        }
        return std::move(document_);
    }

  private:
    VarintReader reader_;
    DomDocument document_;
    std::uint64_t stringCount_ = 0;
    NodeId lastNode_;

    std::optional<StringId> string() {
        const auto value = reader_.read();
        if (!value || *value >= stringCount_) {
            return std::nullopt;
        }
        return static_cast<StringId>(*value);
    }

    std::optional<std::uint64_t> readNode(NodeId parent) {
        const auto length = reader_.read();
        if (!length || *length > reader_.size() - reader_.offset()) {
            return std::nullopt;
        }
        const std::size_t end = reader_.offset() + *length;
        const auto type = string();
        const auto element = string();
        const auto pseudoMask = reader_.read();
        if (!type || !element || !pseudoMask || *pseudoMask >= 128) {
            return std::nullopt;
        }
        const NodeId id = document_.create(document_.text(*type), parent);
        if (!id.valid()) {
            return std::nullopt;
        }
        lastNode_ = id;
        DomNode &node = document_.node(id);
        node.element = *element;
        node.pseudo = static_cast<std::uint8_t>(*pseudoMask);
        const auto classCount = reader_.read();
        if (!classCount || *classCount > maxClasses) {
            return std::nullopt;
        }
        for (std::uint64_t i = 0; i < *classCount; ++i) {
            const auto name = string();
            if (!name || !node.classes.push_back(*name)) {
                return std::nullopt;
            }
        }
        const auto attributeCount = reader_.read();
        if (!attributeCount || *attributeCount > maxAttributes) {
            return std::nullopt;
        }
        for (std::uint64_t i = 0; i < *attributeCount; ++i) {
            const auto name = string();
            const auto value = string();
            if (!name || !value || !node.attributes.push_back({*name, *value})) {
                return std::nullopt;
            }
        }
        const auto bindingCount = reader_.read();
        if (!bindingCount || *bindingCount > maxBindings) {
            return std::nullopt;
        }
        for (std::uint64_t i = 0; i < *bindingCount; ++i) {
            const auto kind = reader_.read();
            const auto source = string();
            const auto path = string();
            if (!kind || *kind >= bindingNames.size() || !source || !path ||
                !node.bindings.push_back({static_cast<BindingKind>(*kind), *source, *path})) {
                return std::nullopt;
            }
        }
        const auto children = reader_.read();
        if (!children || reader_.offset() > end) {
            return std::nullopt;
        }
        reader_.seek(end);
        return children;
    }
};
}

std::vector<std::uint8_t> encodeBinary(const DomDocument &document) {
    std::vector<std::uint8_t> out(magic.begin(), magic.end());
    writeVarint(out, binarySchemaVersion);
    const StringPool &strings = document.strings();
    writeVarint(out, strings.size());
    for (StringId id = 1; id < strings.size(); ++id) {
        const std::string_view text = strings.view(id);
        writeVarint(out, text.size());
        out.insert(out.end(), text.begin(), text.end());
    }
    const NodeId root = document.root();
    writeVarint(out, root.valid() ? document.size() : 0);
    std::vector<std::uint8_t> record;
    if (root.valid()) {
        encodeNode(document, root.index, out, record);
    }
    const std::uint32_t checksum = crc32cChecksum(out);
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(checksum >> (8 * i)));
    }
    out.insert(out.end(), 8, 0);
    return out;
}

std::optional<DomDocument> decodeBinary(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < magic.size() + trailerSize) {
        return std::nullopt;
    }
    const auto body = bytes.first(bytes.size() - trailerSize);
    const auto trailer = bytes.last(trailerSize);
    std::uint32_t stored = 0;
    for (int i = 0; i < 4; ++i) {
        stored |= static_cast<std::uint32_t>(trailer[static_cast<std::size_t>(i)]) << (8 * i);
    }
    for (std::size_t i = 4; i < trailerSize; ++i) {
        if (trailer[i] != 0) {
            return std::nullopt;
        }
    }
    if (stored != crc32cChecksum(body)) {
        return std::nullopt;
    }
    return Decoder(body).run();
}
}
