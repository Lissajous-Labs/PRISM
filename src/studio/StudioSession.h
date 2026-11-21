#pragma once

#include "core/FunctionRef.h"
#include "dom/DomCodec.h"
#include "dom/StyleSheet.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace prism::studio {
struct Snapshot {
    std::string document;
    std::string styles;
};

class StudioSession {
  public:
    StudioSession(dom::DomDocument document, std::string styles);

    [[nodiscard]] dom::DomDocument &document() noexcept { return document_; }
    [[nodiscard]] const dom::DomDocument &document() const noexcept { return document_; }
    [[nodiscard]] const dom::StyleSheet &sheet() const noexcept { return sheet_; }
    [[nodiscard]] dom::StyleSheet &sheet() noexcept { return sheet_; }
    [[nodiscard]] std::string_view styles() const noexcept { return styles_; }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    void edit(FunctionRef<void(dom::DomDocument &)> change);
    [[nodiscard]] bool editStyles(std::string text);
    [[nodiscard]] bool reloadDocument(std::string_view xml);
    [[nodiscard]] bool reloadStyles(std::string text) { return editStyles(std::move(text)); }
    bool undo();
    bool redo();
    [[nodiscard]] bool canUndo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_.empty(); }

    [[nodiscard]] std::vector<std::uint8_t> canonicalBytes() const;
    [[nodiscard]] const dom::StyleError &lastError() const noexcept { return error_; }

  private:
    dom::DomDocument document_;
    dom::StyleSheet sheet_;
    std::string styles_;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    dom::StyleError error_;
    std::uint64_t revision_ = 0;

    [[nodiscard]] Snapshot capture() const;
    [[nodiscard]] bool restore(const Snapshot &snapshot);
    void merge(const dom::DomDocument &next);
};
}
