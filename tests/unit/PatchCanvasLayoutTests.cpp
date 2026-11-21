#include "../support/Pcg.h"
#include "dom/layout/LayoutEngine.h"
#include "dom/layout/PatchCanvasLayout.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace prism;
using namespace prism::dom::layout;

consteval bool decksCollisionFree() {
    for (std::uint32_t n = 1; n <= 16; ++n) {
        const auto deck = deckLayout(n, 320, 240, 8);
        const std::uint32_t expected = n <= 4 ? n : (n <= 8 ? (n + 1) / 2 : 4);
        if (deck.count != n || deckColumns(n) != expected) {
            return false;
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            const DeckSlot &a = deck.slots[i];
            if (a.x0 < 0 || a.y0 < 0 || a.x1 > 320 || a.y1 > 240 || a.x1 <= a.x0 || a.y1 <= a.y0) {
                return false;
            }
            for (std::uint32_t j = 0; j < i; ++j) {
                const DeckSlot &b = deck.slots[j];
                if (a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1) {
                    return false;
                }
            }
        }
    }
    return true;
}

static_assert(decksCollisionFree());
static_assert(deckColumns(5) == 3 && deckColumns(8) == 4 && deckColumns(9) == 4 &&
              deckColumns(16) == 4);

bool dockAt(float scale, float worldDistance, bool expectDock) {
    PatchCanvas canvas(CanvasCamera({0, 0}, scale));
    const ModuleId a = canvas.add({0, 0, 100, 100});
    const ModuleId b = canvas.add({300, 0, 400, 100});
    canvas.beginDrag(b, {0, 0}, DragMode::group);
    const double targetX = 100 + claspGap + worldDistance;
    canvas.dragTo({(targetX - 300) * scale, 0});
    canvas.endDrag();
    const float x0 = canvas.rect(b).x0;
    const bool docked = canvas.docked(a, b);
    return expectDock ? (docked && x0 == 102.0f) : (!docked && std::abs(x0 - targetX) < 1e-3);
}

bool checkDocking() {
    return dockAt(1, 18, true) && dockAt(1, 18.25f, false) && dockAt(1, -18, true) &&
           dockAt(2, 9, true) && dockAt(2, 9.25f, false) && dockAt(0.5f, 36, true) &&
           dockAt(0.5f, 36.5f, false);
}

bool checkTearOff() {
    PatchCanvas canvas(CanvasCamera({0, 0}, 1));
    const ModuleId a = canvas.add({0, 0, 100, 100});
    const ModuleId b = canvas.add({102, 0, 202, 100});
    const ModuleId c = canvas.add({0, 102, 100, 160});
    canvas.beginDrag(b, {0, 0}, DragMode::group);
    canvas.dragTo({-10, 0});
    canvas.endDrag();
    canvas.beginDrag(c, {0, 0}, DragMode::group);
    canvas.dragTo({0, 5});
    canvas.endDrag();
    const bool docked = canvas.docked(a, b) && canvas.docked(a, c) && canvas.rect(b).x0 == 102 &&
                        canvas.rect(c).y0 == 102;
    canvas.beginDrag(a, {0, 0}, DragMode::group);
    canvas.dragTo({50, 30});
    canvas.endDrag();
    const bool rigid = canvas.rect(a).x0 == 50 && canvas.rect(b).x0 == 152 &&
                       canvas.rect(c).y0 == 132 && canvas.rect(b).y0 == 30;
    canvas.beginDrag(b, {0, 0}, DragMode::single);
    canvas.dragTo({28, 0});
    const bool held = canvas.rect(b).x0 == 152 && canvas.docked(a, b);
    canvas.dragTo({28.5f, 0});
    const bool torn = canvas.rect(b).x0 == 180.5f && !canvas.docked(a, b) && canvas.docked(a, c);
    canvas.endDrag();
    return docked && rigid && held && torn && !canvas.docked(a, b);
}

bool checkZoom() {
    CanvasCamera camera({40, -20}, 1);
    const math::Vector2 cursor{200, 100};
    const math::Vector2 world = camera.toWorld(cursor);
    camera.zoomAt(cursor, 1.7f);
    const math::Vector2 after = camera.toWorld(cursor);
    const bool anchored = std::abs(after.x - world.x) < 1e-9 && std::abs(after.y - world.y) < 1e-9;
    camera.zoomAt(cursor, 100);
    const bool upper = camera.scale() == maximumZoom;
    camera.zoomAt(cursor, 1e-4f);
    const math::Vector2 clamped = camera.toWorld(cursor);
    return anchored && upper && camera.scale() == minimumZoom &&
           std::abs(clamped.x - world.x) < 1e-6;
}

bool checkCulling() {
    PatchCanvas canvas(CanvasCamera({0, 0}, 1));
    test::Pcg random(41);
    std::vector<WorldRect> rects;
    for (int i = 0; i < 500; ++i) {
        const auto x = static_cast<float>(random.unit() * 10000 - 5000);
        const auto y = static_cast<float>(random.unit() * 10000 - 5000);
        rects.push_back({x, y, x + 20 + static_cast<float>(random.unit() * 300),
                         y + 20 + static_cast<float>(random.unit() * 200)});
        static_cast<void>(canvas.add(rects.back()));
    }
    std::vector<ModuleId> visible;
    for (int trial = 0; trial < 200; ++trial) {
        const auto x = static_cast<float>(random.unit() * 10000 - 5000);
        const auto y = static_cast<float>(random.unit() * 10000 - 5000);
        const WorldRect view{x, y, x + 800, y + 600};
        canvas.visible(view, visible);
        std::vector<ModuleId> expected;
        for (ModuleId id = 0; id < rects.size(); ++id) {
            if (rects[id].overlaps(view)) {
                expected.push_back(id);
            }
        }
        if (visible != expected) {
            return false;
        }
    }
    return true;
}

bool near(float a, float b) { return std::abs(a - b) < 1e-3f; }

bool checkEngineModes() {
    auto compiled = dom::compileStyleSheet(R"(
Root { display: anchored; }
Pinned { left: 10px; right: 20%; top: 5px; height: 50px; }
Corner { right: 0; bottom: 0; width: 30px; height: 20px; }
Tabs { left: 0; top: 100px; width: 300px; height: 200px; display: tabbed; }
Page { }
List { left: 400px; top: 0; width: 200px; height: 100px; gap: 4px; }
Row { height: 30px; }
Canvas { left: 0; top: 400px; width: 800px; height: 200px; display: patch-canvas; }
Card { width: 100px; height: 50px; }
)");
    if (!compiled.sheet) {
        return false;
    }
    dom::DomDocument document;
    const auto root = document.create("Root");
    const auto pinned = document.create("Pinned", root);
    const auto corner = document.create("Corner", root);
    const auto tabs = document.create("Tabs", root);
    static_cast<void>(document.setAttribute(tabs, "active", "1"));
    const auto first = document.create("Page", tabs);
    const auto second = document.create("Page", tabs);
    const auto list = document.create("List", root);
    static_cast<void>(document.setAttribute(list, "scroll", "vertical"));
    static_cast<void>(document.setAttribute(list, "scroll-y", "500"));
    std::vector<dom::NodeId> rows;
    for (int i = 0; i < 5; ++i) {
        rows.push_back(document.create("Row", list));
    }
    const auto canvasNode = document.create("Canvas", root);
    static_cast<void>(document.setAttribute(canvasNode, "zoom", "2"));
    static_cast<void>(document.setAttribute(canvasNode, "center-x", "-10"));
    const auto card = document.create("Card", canvasNode);
    static_cast<void>(document.setAttribute(card, "canvas-x", "15"));
    static_cast<void>(document.setAttribute(card, "canvas-y", "-5.5"));
    dom::StyleResolver resolver(*compiled.sheet);
    resolver.resolve(document);
    LayoutEngine engine;
    engine.layout(document, resolver, {1000, 800, 1});
    const auto &p = engine.box(pinned);
    const auto &c = engine.box(corner);
    const auto &page = engine.box(second);
    const auto &scroll = engine.scroll(list);
    const auto &lastRow = engine.box(rows.back());
    const auto &cardBox = engine.box(card);
    const bool anchoredOk = near(p.x0, 10) && near(p.x1, 800) && near(p.y0, 5) && near(p.y1, 55) &&
                            near(c.x0, 970) && near(c.y1, 800);
    const bool tabbedOk = !engine.placed(first) && near(page.y0, 128) && near(page.y1, 300) &&
                          near(page.width(), 300);
    const bool scrollOk = near(scroll.extent, 166) && near(scroll.offset, 66) &&
                          near(scroll.reservation, 10) && near(lastRow.y1, 100) &&
                          near(lastRow.x1, 590);
    const bool canvasOk =
        near(cardBox.x0, 50) && near(cardBox.y0, 389) && near(cardBox.width(), 200);
    std::cout << "Modes anchored " << anchoredOk << ", tabbed " << tabbedOk << ", scroll "
              << scrollOk << ", canvas " << canvasOk << '\n';
    return anchoredOk && tabbedOk && scrollOk && canvasOk;
}
}

int main() {
    if (!checkDocking() || !checkTearOff() || !checkZoom() || !checkCulling() ||
        !checkEngineModes()) {
        std::cerr << "Patch canvas layout contract failed\n";
        return 1;
    }
    return 0;
}
