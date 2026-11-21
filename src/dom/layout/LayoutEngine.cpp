#include "dom/layout/LayoutEngine.h"

#include "math/PixelSnap.h"

#include <algorithm>
#include <cmath>

namespace prism::dom::layout {
void LayoutEngine::layout(const DomDocument &document, const StyleResolver &styles,
                          Viewport viewport) {
    const auto none = [](NodeId, float) { return ContentSize{}; };
    layout(document, styles, viewport, MeasureFunction(none));
}

void LayoutEngine::layout(const DomDocument &document, const StyleResolver &styles,
                          Viewport viewport, MeasureFunction measure) {
    document_ = &document;
    styles_ = &styles;
    measure_ = &measure;
    viewport_ = viewport;
    boxes_.assign(document.capacity(), LayoutBox{});
    pixels_.assign(document.capacity(), PixelRect{});
    placed_.assign(document.capacity(), false);
    if (document.root().valid()) {
        layoutNode(document.root(), {0, 0, viewport.width, viewport.height});
    }
    snap();
    measure_ = nullptr;
}

std::optional<float> LayoutEngine::resolve(const Length &length, float reference,
                                           float fontSize) const {
    switch (length.unit) {
    case Unit::automatic:
        return std::nullopt;
    case Unit::percent:
        return std::isfinite(reference) ? std::optional<float>(reference * length.value / 100)
                                        : std::nullopt;
    case Unit::em:
        return length.value * fontSize;
    case Unit::vw:
        return viewport_.width * length.value / 100;
    case Unit::vh:
        return viewport_.height * length.value / 100;
    case Unit::vmin:
        return std::min(viewport_.width, viewport_.height) * length.value / 100;
    default:
        return length.value;
    }
}

ContentSize LayoutEngine::measure(NodeId id, float maxWidth) const {
    return (*measure_)(id, maxWidth);
}

void LayoutEngine::children(NodeId id, std::vector<NodeId> &out) const {
    out.clear();
    document_->forEachChild(id, [&](NodeId child) {
        if (styles_->style(child).layout.display != Keyword::none) {
            out.push_back(child);
        }
    });
}

void LayoutEngine::layoutNode(NodeId id, LayoutBox box) {
    boxes_[id.index] = box;
    placed_[id.index] = true;
    const LayoutBlock &style = styles_->style(id).layout;
    const float inset =
        resolve(style.padding, box.width(), style.fontSize).value_or(0) + style.borderWidth;
    const LayoutBox content{box.x0 + inset, box.y0 + inset,
                            std::max(box.x0 + inset, box.x1 - inset),
                            std::max(box.y0 + inset, box.y1 - inset)};
    std::vector<NodeId> items;
    children(id, items);
    if (items.empty()) {
        return;
    }
    if (style.display == Keyword::grid) {
        layoutGrid(id, content, items);
    } else {
        layoutFlex(id, content, items);
    }
}

void LayoutEngine::layoutFlex(NodeId id, const LayoutBox &content,
                              const std::vector<NodeId> &items) {
    const LayoutBlock &style = styles_->style(id).layout;
    const bool column =
        style.flexDirection == Keyword::column || style.flexDirection == Keyword::columnReverse;
    FlexContainer container;
    container.column = column;
    container.reverse =
        style.flexDirection == Keyword::rowReverse || style.flexDirection == Keyword::columnReverse;
    container.wrap = style.flexWrap == Keyword::wrap;
    container.mainSize = column ? content.height() : content.width();
    container.crossSize = column ? content.width() : content.height();
    const float gap = resolve(style.gap, container.mainSize, style.fontSize).value_or(0);
    container.mainGap = gap;
    container.crossGap = gap;
    container.justify = style.justifyContent;
    container.alignItems = style.alignItems;
    std::vector<FlexItem> inputs(items.size());
    for (std::size_t k = 0; k < items.size(); ++k) {
        const LayoutBlock &child = styles_->style(items[k]).layout;
        const float mainReference = container.mainSize;
        const float crossReference = container.crossSize;
        const Length &mainLength = column ? child.height : child.width;
        const Length &crossLength = column ? child.width : child.height;
        FlexItem &item = inputs[k];
        auto basis = resolve(child.flexBasis, mainReference, child.fontSize);
        if (!basis) {
            basis = resolve(mainLength, mainReference, child.fontSize);
        }
        if (!basis) {
            const ContentSize size = measure(items[k], column ? content.width() : unbounded);
            basis = column ? size.height : size.width;
        }
        item.basis = *basis;
        item.grow = child.flexGrow;
        item.shrink = child.flexShrink;
        item.minMain =
            resolve(column ? child.minHeight : child.minWidth, mainReference, child.fontSize)
                .value_or(0);
        item.maxMain =
            resolve(column ? child.maxHeight : child.maxWidth, mainReference, child.fontSize)
                .value_or(unbounded);
        item.cross = resolve(crossLength, crossReference, child.fontSize);
        item.minCross =
            resolve(column ? child.minWidth : child.minHeight, crossReference, child.fontSize)
                .value_or(0);
        item.maxCross =
            resolve(column ? child.maxWidth : child.maxHeight, crossReference, child.fontSize)
                .value_or(unbounded);
        const float margin = resolve(child.margin, content.width(), child.fontSize).value_or(0);
        item.marginMain = margin;
        item.marginCross = margin;
        item.alignSelf = child.alignSelf;
        item.order = child.order;
    }
    solveFlex(container, inputs, flex_);
    const std::vector<Placement> placements = flex_.items;
    for (std::size_t k = 0; k < items.size(); ++k) {
        const Placement &p = placements[k];
        const LayoutBox box = column
                                  ? LayoutBox{content.x0 + p.crossStart, content.y0 + p.mainStart,
                                              content.x0 + p.crossEnd, content.y0 + p.mainEnd}
                                  : LayoutBox{content.x0 + p.mainStart, content.y0 + p.crossStart,
                                              content.x0 + p.mainEnd, content.y0 + p.crossEnd};
        layoutNode(items[k], box);
    }
}

void LayoutEngine::layoutGrid(NodeId id, const LayoutBox &content,
                              const std::vector<NodeId> &items) {
    const LayoutBlock &style = styles_->style(id).layout;
    GridContainer container;
    container.width = content.width();
    container.height = content.height();
    const float gap = resolve(style.gap, content.width(), style.fontSize).value_or(0);
    container.columnGap = gap;
    container.rowGap = gap;
    if (const auto columns = document_->attribute(id, "grid-columns")) {
        container.columns = parseTracks(*columns).value_or(std::vector<Track>{});
    }
    if (const auto rows = document_->attribute(id, "grid-rows")) {
        container.rows = parseTracks(*rows).value_or(std::vector<Track>{});
    }
    std::vector<GridItem> inputs(items.size());
    for (std::size_t k = 0; k < items.size(); ++k) {
        const LayoutBlock &child = styles_->style(items[k]).layout;
        GridItem &item = inputs[k];
        if (const auto column = document_->attribute(items[k], "grid-column")) {
            item.column = parseGridLine(*column).value_or(GridLine{});
        }
        if (const auto row = document_->attribute(items[k], "grid-row")) {
            item.row = parseGridLine(*row).value_or(GridLine{});
        }
        const ContentSize measured = measure(items[k], content.width());
        item.contentWidth =
            resolve(child.width, content.width(), child.fontSize).value_or(measured.width);
        item.contentHeight =
            resolve(child.height, content.height(), child.fontSize).value_or(measured.height);
    }
    solveGrid(container, inputs, grid_);
    const std::vector<Placement> placements = grid_.items;
    for (std::size_t k = 0; k < items.size(); ++k) {
        const Placement &p = placements[k];
        layoutNode(items[k], {content.x0 + p.mainStart, content.y0 + p.crossStart,
                              content.x0 + p.mainEnd, content.y0 + p.crossEnd});
    }
}

void LayoutEngine::snap() {
    for (std::size_t i = 0; i < boxes_.size(); ++i) {
        if (!placed_[i]) {
            continue;
        }
        const LayoutBox &b = boxes_[i];
        const auto edge = [&](float value) {
            return math::snapBoundary(static_cast<double>(value), viewport_.scale).value_or(0);
        };
        pixels_[i] = {edge(b.x0), edge(b.y0), edge(b.x1), edge(b.y1)};
    }
}
}
