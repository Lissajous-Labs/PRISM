#include "dom/layout/LayoutEngine.h"
#include "math/PixelSnap.h"

#include <cmath>
#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace prism;
using namespace prism::dom::layout;
using dom::Keyword;

bool near(float a, float b) { return std::abs(a - b) < 1e-3f; }

struct Case {
    std::string name;
    bool passed;
};

std::vector<Case> cases;

void expect(const std::string &name, bool passed) { cases.push_back({name, passed}); }

std::vector<FlexItem> basisItems(std::initializer_list<float> bases) {
    std::vector<FlexItem> items;
    for (const float basis : bases) {
        FlexItem item;
        item.basis = basis;
        items.push_back(item);
    }
    return items;
}

bool mains(const FlexResult &result, std::initializer_list<float> starts,
           std::initializer_list<float> sizes) {
    std::size_t i = 0;
    bool ok = result.items.size() == starts.size();
    for (auto s = starts.begin(), w = sizes.begin(); ok && s != starts.end(); ++s, ++w, ++i) {
        ok = near(result.items[i].mainStart, *s) &&
             near(result.items[i].mainEnd - result.items[i].mainStart, *w);
    }
    return ok;
}

void flexCases() {
    FlexResult result;
    FlexContainer container;
    container.mainSize = 600;
    container.crossSize = 100;
    auto items = basisItems({100, 100, 100});
    items[0].grow = 1;
    items[1].grow = 2;
    items[2].grow = 3;
    solveFlex(container, items, result);
    expect("grow ratios", mains(result, {0, 150, 350}, {150, 200, 250}));

    container.mainSize = 300;
    items = basisItems({200, 200});
    items[1].shrink = 3;
    solveFlex(container, items, result);
    expect("shrink weighted by basis", mains(result, {0, 175}, {175, 125}));

    container.mainSize = 500;
    items = basisItems({100, 100, 100});
    for (auto &item : items) {
        item.grow = 1;
    }
    items[0].maxMain = 120;
    solveFlex(container, items, result);
    expect("max violation freeze loop",
           mains(result, {0, 120, 310}, {120, 190, 190}) && result.freezeIterations == 2);

    container.mainSize = 200;
    items = basisItems({150, 150});
    items[0].minMain = 120;
    solveFlex(container, items, result);
    expect("min violation freeze loop", mains(result, {0, 120}, {120, 80}));

    container.mainSize = 400;
    items = basisItems({100, 100});
    items[0].grow = 0.25f;
    items[1].grow = 0.25f;
    solveFlex(container, items, result);
    expect("fractional grow sum below one", mains(result, {0, 150}, {150, 150}));

    container.mainSize = 250;
    container.wrap = true;
    container.mainGap = 10;
    container.crossGap = 10;
    container.crossSize = unbounded;
    items = basisItems({100, 100, 100, 100});
    for (auto &item : items) {
        item.cross = 40;
    }
    solveFlex(container, items, result);
    expect("wrap with gaps", result.lines == 2 && near(result.items[2].crossStart, 50) &&
                                 near(result.items[3].mainStart, 110) &&
                                 near(result.usedCross, 90));
    container.wrap = false;
    container.mainGap = 0;
    container.crossGap = 0;

    container.mainSize = 400;
    container.crossSize = 100;
    items = basisItems({50, 50, 50});
    const std::pair<Keyword, std::vector<float>> justify[] = {
        {Keyword::start, {0, 50, 100}},
        {Keyword::end, {250, 300, 350}},
        {Keyword::center, {125, 175, 225}},
        {Keyword::spaceBetween, {0, 175, 350}},
        {Keyword::spaceAround, {41.6667f, 175, 308.333f}},
        {Keyword::spaceEvenly, {62.5f, 175, 287.5f}}};
    for (const auto &[mode, starts] : justify) {
        container.justify = mode;
        solveFlex(container, items, result);
        bool ok = true;
        for (std::size_t i = 0; i < 3; ++i) {
            ok = ok && near(result.items[i].mainStart, starts[i]);
        }
        expect("justify " + std::to_string(static_cast<int>(mode)), ok);
    }
    container.justify = Keyword::start;

    items = basisItems({50, 50, 50});
    items[0].cross = 40;
    items[1].cross = 40;
    items[1].alignSelf = Keyword::center;
    items[2].marginCross = 5;
    items[2].alignSelf = Keyword::stretch;
    container.alignItems = Keyword::end;
    solveFlex(container, items, result);
    expect("cross alignment and stretch",
           near(result.items[0].crossStart, 60) && near(result.items[1].crossStart, 30) &&
               near(result.items[2].crossStart, 5) && near(result.items[2].crossEnd, 95));
    container.alignItems = Keyword::stretch;

    container.reverse = true;
    items = basisItems({100, 50});
    solveFlex(container, items, result);
    expect("row reverse",
           near(result.items[0].mainStart, 300) && near(result.items[1].mainStart, 250));
    container.reverse = false;

    items = basisItems({10, 20, 30});
    items[0].order = 2;
    items[2].order = -1;
    solveFlex(container, items, result);
    expect("order property", near(result.items[2].mainStart, 0) &&
                                 near(result.items[1].mainStart, 30) &&
                                 near(result.items[0].mainStart, 50));

    container.wrap = true;
    container.mainSize = 100;
    container.crossSize = 200;
    container.alignContent = Keyword::spaceBetween;
    items = basisItems({100, 100});
    for (auto &item : items) {
        item.cross = 50;
    }
    solveFlex(container, items, result);
    expect("align-content space-between",
           near(result.items[0].crossStart, 0) && near(result.items[1].crossStart, 150));
}

std::vector<float> trackSizes(const GridResult &result, bool columns) {
    const auto &edges = columns ? result.columnEdges : result.rowEdges;
    std::vector<float> sizes;
    for (std::size_t i = 0; i < edges.size(); i += 2) {
        sizes.push_back(edges[i + 1] - edges[i]);
    }
    return sizes;
}

bool sizesAre(const std::vector<float> &actual, std::initializer_list<float> expected) {
    if (actual.size() != expected.size()) {
        return false;
    }
    std::size_t i = 0;
    for (const float value : expected) {
        if (!near(actual[i++], value)) {
            return false;
        }
    }
    return true;
}

void gridCases() {
    GridResult result;
    GridContainer container;
    container.width = 400;
    container.columns = *parseTracks("100px 1fr 2fr");
    solveGrid(container, {}, result);
    expect("fixed plus fractions", sizesAre(trackSizes(result, true), {100, 100, 200}));

    container.width = 420;
    container.columnGap = 20;
    container.columns = *parseTracks("repeat(2, 1fr)");
    solveGrid(container, {}, result);
    expect("repeat with gap",
           sizesAre(trackSizes(result, true), {200, 200}) && near(result.columnEdges[2], 220));
    container.columnGap = 0;

    container.width = 200;
    container.columns = *parseTracks("minmax(150px, 1fr) 1fr");
    solveGrid(container, {}, result);
    expect("fraction min clamping", sizesAre(trackSizes(result, true), {150, 50}));

    container.width = 400;
    container.columns = *parseTracks("25% auto 1fr");
    std::vector<GridItem> items(3);
    items[1].contentWidth = 60;
    solveGrid(container, items, result);
    expect("percent auto fraction", sizesAre(trackSizes(result, true), {100, 60, 240}));

    container.width = 300;
    container.columns = *parseTracks("auto auto");
    items.assign(2, GridItem{});
    items[0].contentWidth = 50;
    items[1].contentWidth = 70;
    solveGrid(container, items, result);
    expect("auto tracks stretch", sizesAre(trackSizes(result, true), {140, 160}));

    container.columns = *parseTracks("0.25fr 0.25fr");
    container.width = 400;
    solveGrid(container, {}, result);
    expect("fraction sum below one", sizesAre(trackSizes(result, true), {100, 100}));

    container.columns = *parseTracks("repeat(3, 100px)");
    container.rows = *parseTracks("50px");
    items.assign(6, GridItem{});
    items[1].column.span = 2;
    items[3].column = *parseGridLine("2 / 4");
    items[3].row = *parseGridLine("3");
    items[4].contentHeight = 80;
    solveGrid(container, items, result);
    const bool placed = result.resolvedColumns[0].start == 0 &&
                        result.resolvedColumns[1].start == 1 && result.resolvedRows[2].start == 1 &&
                        result.resolvedColumns[3].start == 1 && result.resolvedRows[3].start == 2 &&
                        result.resolvedRows[4].start == 1 && result.resolvedColumns[4].start == 1 &&
                        result.resolvedRows[5].start == 1 && result.resolvedColumns[5].start == 2;
    expect("auto placement with spans", placed && sizesAre(trackSizes(result, false), {50, 80, 0}));

    expect("track parse errors", !parseTracks("1fr nonsense") &&
                                     !parseTracks("minmax(1fr, 10px)") && !parseGridLine("3 / 2") &&
                                     !parseGridLine("span 0"));
}

void seamCases() {
    for (const double scale : {1.0, 1.25, 1.5, 1.75, 2.0, 2.25, 3.0}) {
        FlexContainer container;
        container.mainSize = 997.3f;
        container.crossSize = 33.3f;
        auto items = basisItems({13.7f, 101.1f, 77.7f, 3.3f, 250.9f, 0.4f, 61});
        for (std::size_t i = 0; i < items.size(); ++i) {
            items[i].grow = static_cast<float>(i % 3) + 0.5f;
        }
        FlexResult result;
        solveFlex(container, items, result);
        bool seamless = true;
        std::int64_t total = 0;
        const float origin = 17.37f;
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto x0 = *math::snapBoundary(origin + result.items[i].mainStart, scale);
            const auto x1 = *math::snapBoundary(origin + result.items[i].mainEnd, scale);
            total += x1 - x0;
            const double ideal = scale * (result.items[i].mainEnd - result.items[i].mainStart);
            seamless = seamless && std::abs(static_cast<double>(x1 - x0) - ideal) < 1;
            if (i + 1 < items.size()) {
                seamless = seamless &&
                           x1 == *math::snapBoundary(origin + result.items[i + 1].mainStart, scale);
            }
        }
        const auto span = *math::snapBoundary(origin + container.mainSize, scale) -
                          *math::snapBoundary(origin, scale);
        expect("zero seam at scale " + std::to_string(scale), seamless && total == span);
    }
}

void engineCases() {
    auto compiled = dom::compileStyleSheet(R"(
Editor { display: flex; flex-direction: column; padding: 10px; gap: 6px; }
Header { height: 40px; }
Body { flex-grow: 1; display: grid; gap: 4px; }
Cell { }
Footer { height: 10%; }
Side { display: flex; flex-grow: 1; }
Knob { flex-grow: 1; margin: 2px; }
)");
    if (!compiled.sheet) {
        expect("engine stylesheet", false);
        return;
    }
    dom::DomDocument document;
    const auto editor = document.create("Editor");
    const auto header = document.create("Header", editor);
    const auto body = document.create("Body", editor);
    static_cast<void>(document.setAttribute(body, "grid-columns", "120px 1fr 1fr"));
    static_cast<void>(document.setAttribute(body, "grid-rows", "1fr 1fr"));
    std::vector<dom::NodeId> cells;
    for (int i = 0; i < 6; ++i) {
        cells.push_back(document.create("Cell", body));
    }
    const auto footer = document.create("Footer", editor);
    const auto side = document.create("Side", cells[5]);
    const auto knobA = document.create("Knob", side);
    const auto knobB = document.create("Knob", side);
    dom::StyleResolver resolver(*compiled.sheet);
    resolver.setViewport(800, 600);
    resolver.resolve(document);
    LayoutEngine engine;
    engine.layout(document, resolver, {800, 600, 1.5});
    const auto &h = engine.box(header);
    const auto &b = engine.box(body);
    const auto &f = engine.box(footer);
    const auto &c1 = engine.box(cells[1]);
    const auto &c4 = engine.box(cells[4]);
    const auto &ka = engine.box(knobA);
    const auto &kb = engine.box(knobB);
    const bool boxes =
        near(h.x0, 10) && near(h.y0, 10) && near(h.width(), 780) && near(h.height(), 40) &&
        near(f.height(), 58) && near(f.y1, 590) && near(b.y0, 56) && near(b.y1, f.y0 - 6) &&
        near(c1.x0, b.x0 + 124) && near(c1.width(), (780 - 120 - 8) / 2.0f) &&
        near(c4.y0, b.y0 + (b.height() - 4) / 2 + 4) && near(ka.x0, engine.box(cells[5]).x0 + 2) &&
        near(kb.x1, engine.box(cells[5]).x1 - 2) && near(ka.width(), kb.width());
    const auto &p = engine.pixels(header);
    const auto &pb = engine.pixels(body);
    const bool snapped = p.x0 == 15 && p.x1 == 1185 && pb.y0 == 84 && engine.placed(knobB);
    expect("engine composes flex and grid", boxes && snapped);
}
}

int main() {
    flexCases();
    gridCases();
    seamCases();
    engineCases();
    bool ok = true;
    for (const Case &c : cases) {
        if (!c.passed) {
            std::cerr << "Layout case failed: " << c.name << '\n';
            ok = false;
        }
    }
    std::cout << cases.size() << " layout conformance cases\n";
    return ok ? 0 : 1;
}
