#include "studio/StudioOverlay.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace prism::studio {
namespace {
float readNumber(const dom::DomDocument &document, dom::NodeId node, std::string_view name) {
    const auto text = document.attribute(node, name);
    return text ? std::strtof(std::string(*text).c_str(), nullptr) : 0.0f;
}

void writeNumber(dom::DomDocument &document, dom::NodeId node, std::string_view name, float value) {
    char buffer[32];
    const int length = std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
    static_cast<void>(document.setAttribute(
        node, name, std::string_view(buffer, static_cast<std::size_t>(std::max(length, 0)))));
}
}

void StudioOverlay::deepest(const dom::layout::LayoutEngine &layout, dom::NodeId node, float x,
                            float y, dom::NodeId &best) const {
    const auto &document = session_->document();
    if (!layout.placed(node)) {
        return;
    }
    const auto &box = layout.box(node);
    if (x < box.x0 || x >= box.x1 || y < box.y0 || y >= box.y1) {
        return;
    }
    best = node;
    document.forEachChild(node, [&](dom::NodeId child) { deepest(layout, child, x, y, best); });
}

dom::NodeId StudioOverlay::hitTest(const dom::layout::LayoutEngine &layout, float x,
                                   float y) const {
    dom::NodeId best{};
    deepest(layout, session_->document().root(), x, y, best);
    return best;
}

bool StudioOverlay::beginDrag(const dom::layout::LayoutEngine &layout, float x, float y) {
    const auto node = hitTest(layout, x, y);
    if (!node.valid() || node == session_->document().root()) {
        return false;
    }
    auto &document = session_->document();
    dragging_ = node;
    selected_ = node;
    startX_ = x;
    startY_ = y;
    originX_ = readNumber(document, node, "canvas-x");
    originY_ = readNumber(document, node, "canvas-y");
    session_->edit([&](dom::DomDocument &edited) {
        edited.setPseudo(node, edited.node(node).pseudo | dom::pseudo::active);
    });
    return true;
}

void StudioOverlay::dragTo(float x, float y) {
    auto &document = session_->document();
    if (!document.contains(dragging_)) {
        dragging_ = {};
        return;
    }
    writeNumber(document, dragging_, "canvas-x", originX_ + x - startX_);
    writeNumber(document, dragging_, "canvas-y", originY_ + y - startY_);
}

void StudioOverlay::endDrag() {
    auto &document = session_->document();
    if (document.contains(dragging_)) {
        document.setPseudo(dragging_, document.node(dragging_).pseudo &
                                          static_cast<std::uint8_t>(~dom::pseudo::active));
    }
    dragging_ = {};
}

void StudioOverlay::draw(render::DrawList &list, const dom::layout::LayoutEngine &layout) const {
    const auto &document = session_->document();
    const auto outline = [&](dom::NodeId node, std::uint32_t color, double width) {
        if (!document.contains(node) || !layout.placed(node)) {
            return;
        }
        const auto &box = layout.box(node);
        list.roundedRect({box.x0, box.y0, box.x1, box.y1}, {}, render::packColor(0, 0, 0, 0),
                         render::BorderStyle{{width, width, width, width}, color});
    };
    outline(hovered_, render::packColor(120, 200, 255, 200), 1);
    outline(selected_, render::packColor(255, 170, 60, 255), 2);
}
}
