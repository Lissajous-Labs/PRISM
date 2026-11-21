#include "../support/AllocGuard.h"
#include "../support/Pcg.h"
#include "studio/GpuProfilerHud.h"
#include "studio/HotReloader.h"
#include "studio/ProvenanceInspector.h"
#include "studio/StudioOverlay.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace prism;

constexpr std::string_view styles =
    R"(Editor { display: patch-canvas; width: 800px; height: 600px; }
Module { width: 100px; height: 60px; background: #333333; }
#lead { background: #aa3333; }
)";

constexpr std::string_view document =
    R"(<Editor><Module id="lead" canvas-x="40" canvas-y="40"/><Module canvas-x="300" canvas-y="200"/></Editor>)";

void writeFile(const std::filesystem::path &path, std::string_view text) {
    std::ofstream(path, std::ios::binary) << text;
}

struct Frame {
    dom::StyleResolver resolver;
    dom::layout::LayoutEngine layout;
    explicit Frame(studio::StudioSession &session) : resolver(session.sheet()) {
        resolver.setViewport(800, 600);
        resolver.resolve(session.document());
        layout.layout(session.document(), resolver, {800, 600});
    }
};

dom::NodeId find(const dom::DomDocument &doc, std::string_view element) {
    dom::NodeId found{};
    const auto visit = [&](auto &&self, dom::NodeId node) -> void {
        if (doc.text(doc.node(node).element) == element) {
            found = node;
        }
        doc.forEachChild(node, [&](dom::NodeId child) { self(self, child); });
    };
    visit(visit, doc.root());
    return found;
}

bool checkDragAndReload() {
    auto parsed = dom::decodeXml(document);
    if (!parsed) {
        std::cerr << "Document did not parse\n";
        return false;
    }
    studio::StudioSession session(std::move(*parsed), std::string(styles));
    const auto lead = find(session.document(), "lead");
    studio::StudioOverlay overlay(session);
    Frame before(session);
    const auto start = before.layout.box(lead);
    if (overlay.hitTest(before.layout, start.x0 + 5, start.y0 + 5) != lead ||
        !overlay.beginDrag(before.layout, start.x0 + 5, start.y0 + 5)) {
        std::cerr << "Hit test missed the dragged module\n";
        return false;
    }
    overlay.dragTo(start.x0 + 45, start.y0 + 25);
    const auto directory = std::filesystem::temp_directory_path() / "prism-studio-tests";
    std::filesystem::create_directories(directory);
    writeFile(directory / "doc.xml", document);
    writeFile(directory / "style.css", styles);
    studio::HotReloader reloader(session);
    reloader.watch(directory / "doc.xml", studio::WatchKind::document);
    reloader.watch(directory / "style.css", studio::WatchKind::styles);
    std::string edited(styles);
    edited += "Module { border-radius: 6px; }\n";
    writeFile(directory / "style.css", edited);
    writeFile(
        directory / "doc.xml",
        R"(<Editor><Module id="lead" canvas-x="80" canvas-y="60"/><Module canvas-x="300" canvas-y="200"/><Module canvas-x="500" canvas-y="100"/></Editor>)");
    const auto report = reloader.poll();
    writeFile(directory / "style.css", "Module { width: nope; }");
    const auto rejected = reloader.poll();
    const bool kept = session.document().contains(lead) && overlay.dragging() == lead &&
                      (session.document().node(lead).pseudo & dom::pseudo::active) != 0;
    overlay.dragTo(start.x0 + 15, start.y0 + 5);
    overlay.endDrag();
    Frame after(session);
    const auto moved = after.layout.box(lead);
    std::cout << "Reload applied " << report.applied << ", rejected " << rejected.rejected
              << "; drag preserved " << kept << "; module moved to " << moved.x0 << "\n";
    return report.changed == 2 && report.applied == 2 && rejected.rejected == 1 && kept &&
           std::abs(moved.x0 - (start.x0 + 10)) < 1e-3 && session.document().size() == 4 &&
           (session.document().node(lead).pseudo & dom::pseudo::active) == 0;
}

bool checkUndoRedo() {
    auto parsed = dom::decodeXml(document);
    studio::StudioSession session(std::move(*parsed), std::string(styles));
    test::Pcg random(17);
    std::vector<std::vector<std::uint8_t>> history{session.canonicalBytes()};
    std::vector<std::string> styleHistory{std::string(session.styles())};
    std::vector<dom::NodeId> nodes{session.document().root()};
    for (int step = 0; step < 200; ++step) {
        const auto choice = random.next() % 4;
        const auto target = nodes[random.next() % nodes.size()];
        if (!session.document().contains(target)) {
            continue;
        }
        if (choice == 0) {
            session.edit(
                [&](dom::DomDocument &doc) { nodes.push_back(doc.create("Module", target)); });
        } else if (choice == 1) {
            session.edit([&](dom::DomDocument &doc) {
                static_cast<void>(
                    doc.setAttribute(target, "canvas-x", std::to_string(random.next() % 700)));
            });
        } else if (choice == 2) {
            session.edit([&](dom::DomDocument &doc) {
                static_cast<void>(doc.addClass(target, "c" + std::to_string(random.next() % 5)));
            });
        } else {
            const std::string text = std::string(styles) + ".c" +
                                     std::to_string(random.next() % 5) + " { opacity: 0." +
                                     std::to_string(random.next() % 10) + "; }\n";
            if (!session.editStyles(text)) {
                return false;
            }
        }
        history.push_back(session.canonicalBytes());
        styleHistory.emplace_back(session.styles());
    }
    for (std::size_t i = history.size() - 1; i > 0; --i) {
        if (!session.undo() || session.canonicalBytes() != history[i - 1] ||
            session.styles() != styleHistory[i - 1]) {
            std::cerr << "Undo diverged at step " << i << '\n';
            return false;
        }
    }
    for (std::size_t i = 1; i < history.size(); ++i) {
        if (!session.redo() || session.canonicalBytes() != history[i] ||
            session.styles() != styleHistory[i]) {
            std::cerr << "Redo diverged at step " << i << '\n';
            return false;
        }
    }
    std::cout << history.size() - 1 << " edits undone and redone byte-identically\n";
    return !session.canRedo();
}

bool checkProvenance() {
    auto parsed = dom::decodeXml(document);
    studio::StudioSession session(std::move(*parsed), std::string(styles));
    Frame frame(session);
    const auto lead = find(session.document(), "lead");
    const auto traces = studio::traceNode(frame.resolver, session.sheet(), lead);
    const auto &background = traces[static_cast<std::size_t>(dom::Property::background)];
    const auto &width = traces[static_cast<std::size_t>(dom::Property::width)];
    const auto report =
        studio::describeNode(session.document(), frame.resolver, session.sheet(), lead);
    return background.origin == studio::Origin::rule && background.line == 3 &&
           background.ids == 1 && width.origin == studio::Origin::rule && width.line == 2 &&
           width.types == 1 && report.find("background: rule") != std::string::npos;
}

bool checkProfiler() {
    telemetry::FrameStatistics statistics(128);
    telemetry::Moments left, right;
    for (int i = 1; i <= 100; ++i) {
        statistics.add(i);
        (i <= 37 ? left : right).add(i);
    }
    const auto merged = left.merged(right);
    const bool exact = std::abs(statistics.quantile(0.5) - 50.5) < 1e-12 &&
                       std::abs(statistics.quantile(0.99) - 99.01) < 1e-9 &&
                       std::abs(statistics.moments().variance() - 841.6666666666666) < 1e-9 &&
                       std::abs(merged.variance() - statistics.moments().variance()) < 1e-9 &&
                       std::abs(merged.mean - 50.5) < 1e-12;
    studio::GpuProfilerHud hud;
    render::DrawList list;
    for (int i = 0; i < 300; ++i) {
        hud.record({0.2 + 0.001 * i, 0.5});
    }
    hud.draw(list, {0, 0, 240, 80}, 2.5);
    std::size_t allocations = 0;
    for (int frame = 0; frame < 1000; ++frame) {
        try {
            test::AllocGuard guard;
            list.clear();
            hud.record({0.25, 0.6});
            hud.draw(list, {0, 0, 240, 80}, 2.5);
        } catch (const std::bad_alloc &) {
            ++allocations;
        }
    }
    return exact && allocations == 0 && list.primitives().size() > 200;
}
}

int main() {
    if (!checkDragAndReload() || !checkUndoRedo() || !checkProvenance() || !checkProfiler()) {
        std::cerr << "Studio contract failed\n";
        return 1;
    }
    return 0;
}
