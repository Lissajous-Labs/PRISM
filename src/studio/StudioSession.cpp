#include "studio/StudioSession.h"

#include <utility>

namespace prism::studio {
StudioSession::StudioSession(dom::DomDocument document, std::string styles)
    : document_(std::move(document)), styles_(std::move(styles)) {
    auto compiled = dom::compileStyleSheet(styles_);
    if (compiled.sheet) {
        sheet_ = std::move(*compiled.sheet);
    } else {
        error_ = compiled.error;
    }
}

Snapshot StudioSession::capture() const { return {dom::encodeXml(document_), styles_}; }

void StudioSession::merge(const dom::DomDocument &next) {
    std::vector<std::pair<dom::NodeId, std::uint8_t>> pseudo;
    for (std::uint32_t i = 0; i < document_.capacity(); ++i) {
        const auto id = document_.handle(i);
        if (document_.contains(id) && document_.node(id).pseudo != 0) {
            pseudo.emplace_back(id, document_.node(id).pseudo);
        }
    }
    static_cast<void>(dom::reconcile(document_, next));
    for (const auto &[id, mask] : pseudo) {
        if (document_.contains(id)) {
            document_.setPseudo(id, mask);
        }
    }
    ++revision_;
}

bool StudioSession::restore(const Snapshot &snapshot) {
    auto decoded = dom::decodeXml(snapshot.document);
    auto compiled = dom::compileStyleSheet(snapshot.styles);
    if (!decoded || !compiled.sheet) {
        return false;
    }
    merge(*decoded);
    styles_ = snapshot.styles;
    sheet_ = std::move(*compiled.sheet);
    return true;
}

void StudioSession::edit(FunctionRef<void(dom::DomDocument &)> change) {
    undo_.push_back(capture());
    redo_.clear();
    change(document_);
    ++revision_;
}

bool StudioSession::editStyles(std::string text) {
    auto compiled = dom::compileStyleSheet(text);
    if (!compiled.sheet) {
        error_ = compiled.error;
        return false;
    }
    undo_.push_back(capture());
    redo_.clear();
    styles_ = std::move(text);
    sheet_ = std::move(*compiled.sheet);
    error_ = {};
    ++revision_;
    return true;
}

bool StudioSession::reloadDocument(std::string_view xml) {
    auto decoded = dom::decodeXml(xml);
    if (!decoded) {
        error_ = {0, "Document could not be parsed"};
        return false;
    }
    undo_.push_back(capture());
    redo_.clear();
    merge(*decoded);
    error_ = {};
    return true;
}

bool StudioSession::undo() {
    if (undo_.empty()) {
        return false;
    }
    redo_.push_back(capture());
    const Snapshot previous = std::move(undo_.back());
    undo_.pop_back();
    return restore(previous);
}

bool StudioSession::redo() {
    if (redo_.empty()) {
        return false;
    }
    undo_.push_back(capture());
    const Snapshot next = std::move(redo_.back());
    redo_.pop_back();
    return restore(next);
}

std::vector<std::uint8_t> StudioSession::canonicalBytes() const {
    const auto canonical = dom::decodeXml(dom::encodeXml(document_));
    return canonical ? dom::encodeBinary(*canonical) : std::vector<std::uint8_t>{};
}
}
