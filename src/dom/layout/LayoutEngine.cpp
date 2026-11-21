#include "dom/layout/LayoutEngine.h"

#include "dom/layout/AnchoredSolver.h"
#include "dom/layout/PatchCanvasLayout.h"
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
    scroll_.assign(document.capacity(), ScrollInfo{});
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
    if (document_->attribute(id, "scroll") == std::optional<std::string_view>("vertical")) {
        layoutScroll(id, content, items);
    } else if (style.display == Keyword::grid) {
        layoutGrid(id, content, items);
    } else if (style.display == Keyword::anchored) {
        layoutAnchored(content, items);
    } else if (style.display == Keyword::tabbed) {
        layoutTabbed(id, content, items);
    } else if (style.display == Keyword::patchCanvas) {
        layoutCanvas(id, content, items);
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

float LayoutEngine::numberAttribute(NodeId id, std::string_view name, float fallback) const {
    const auto text = document_->attribute(id, name);
    if (!text) {
        return fallback;
    }
    std::string_view digits = *text;
    const bool negative = !digits.empty() && digits.front() == '-';
    digits.remove_prefix(negative ? 1 : 0);
    float value = 0;
    float divisor = 0;
    for (const char c : digits) {
        if (c == '.' && divisor == 0) {
            divisor = 1;
        } else if (c >= '0' && c <= '9') {
            value = value * 10 + static_cast<float>(c - '0');
            divisor *= 10;
        } else {
            return fallback;
        }
    }
    if (digits.empty()) {
        return fallback;
    }
    value = divisor > 0 ? value / divisor : value;
    return negative ? -value : value;
}

void LayoutEngine::layoutAnchored(const LayoutBox &content, const std::vector<NodeId> &items) {
    for (const NodeId item : items) {
        const LayoutBlock &child = styles_->style(item).layout;
        const float w = content.width();
        const float h = content.height();
        const AnchoredItem anchored{
            resolve(child.left, w, child.fontSize),  resolve(child.top, h, child.fontSize),
            resolve(child.right, w, child.fontSize), resolve(child.bottom, h, child.fontSize),
            resolve(child.width, w, child.fontSize), resolve(child.height, h, child.fontSize)};
        const Placement p = solveAnchored(anchored, w, h);
        layoutNode(item, {content.x0 + p.mainStart, content.y0 + p.crossStart,
                          content.x0 + p.mainEnd, content.y0 + p.crossEnd});
    }
}

void LayoutEngine::layoutTabbed(NodeId id, const LayoutBox &content,
                                const std::vector<NodeId> &items) {
    const auto active = static_cast<std::size_t>(std::max(0.0f, numberAttribute(id, "active", 0)));
    const float bar = numberAttribute(id, "tab-height", 28);
    if (active < items.size()) {
        layoutNode(items[active],
                   {content.x0, std::min(content.y1, content.y0 + bar), content.x1, content.y1});
    }
}

void LayoutEngine::layoutScroll(NodeId id, const LayoutBox &content,
                                const std::vector<NodeId> &items) {
    const LayoutBlock &style = styles_->style(id).layout;
    const float gap = resolve(style.gap, content.height(), style.fontSize).value_or(0);
    const float bar = numberAttribute(id, "scrollbar-width", 10);
    const auto heights = [&](float width) {
        std::vector<float> result;
        for (const NodeId item : items) {
            const LayoutBlock &child = styles_->style(item).layout;
            const auto fixed = resolve(child.height, content.height(), child.fontSize);
            result.push_back(fixed ? *fixed : measure(item, width).height);
        }
        return result;
    };
    const ScrollPlan plan = planScroll(heights(content.width()), content.height(), gap, bar);
    const auto finalHeights = plan.reservation > 0 ? heights(content.width() - plan.reservation)
                                                   : heights(content.width());
    float extent = 0;
    for (std::size_t i = 0; i < finalHeights.size(); ++i) {
        extent += finalHeights[i] + (i > 0 ? gap : 0);
    }
    const float offset = clampScroll(numberAttribute(id, "scroll-y", 0), extent, content.height());
    scroll_[id.index] = {extent, offset, plan.reservation};
    std::vector<Placement> placements;
    stackColumn(finalHeights, gap, offset, placements);
    for (std::size_t i = 0; i < items.size(); ++i) {
        layoutNode(items[i], {content.x0, content.y0 + placements[i].crossStart,
                              content.x1 - plan.reservation, content.y0 + placements[i].crossEnd});
    }
}

void LayoutEngine::layoutCanvas(NodeId id, const LayoutBox &content,
                                const std::vector<NodeId> &items) {
    const CanvasCamera camera(
        {numberAttribute(id, "center-x", 0), numberAttribute(id, "center-y", 0)},
        std::clamp(numberAttribute(id, "zoom", 1), minimumZoom, maximumZoom));
    for (const NodeId item : items) {
        const LayoutBlock &child = styles_->style(item).layout;
        const math::Vector2 origin{numberAttribute(item, "canvas-x", 0),
                                   numberAttribute(item, "canvas-y", 0)};
        const float w = resolve(child.width, unbounded, child.fontSize).value_or(0);
        const float h = resolve(child.height, unbounded, child.fontSize).value_or(0);
        const math::Vector2 a = camera.toScreen(origin);
        const math::Vector2 b = camera.toScreen(origin + math::Vector2{w, h});
        layoutNode(item,
                   {content.x0 + static_cast<float>(a.x), content.y0 + static_cast<float>(a.y),
                    content.x0 + static_cast<float>(b.x), content.y0 + static_cast<float>(b.y)});
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
