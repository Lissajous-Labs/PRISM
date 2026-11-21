#pragma once

#include "dom/DomDocument.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prism::dom {
inline constexpr std::uint64_t binarySchemaVersion = 1;
inline constexpr std::size_t maxDocumentDepth = 256;

[[nodiscard]] std::vector<std::uint8_t> encodeBinary(const DomDocument &document);
[[nodiscard]] std::optional<DomDocument> decodeBinary(std::span<const std::uint8_t> bytes);

[[nodiscard]] std::string encodeXml(const DomDocument &document);
[[nodiscard]] std::optional<DomDocument> decodeXml(std::string_view text);

[[nodiscard]] std::string encodeJson(const DomDocument &document);
[[nodiscard]] std::optional<DomDocument> decodeJson(std::string_view text);

struct ReconcileStats {
    std::size_t created = 0;
    std::size_t removed = 0;
    std::size_t updated = 0;
    std::size_t kept = 0;
};

ReconcileStats reconcile(DomDocument &live, const DomDocument &next);
}
