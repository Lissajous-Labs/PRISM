#include "../support/Pcg.h"
#include "dom/StyleResolver.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {
using namespace prism;
using dom::Property;

consteval bool tableInjective() {
    for (std::size_t i = 0; i < dom::propertyNames.size(); ++i) {
        if (dom::propertyTable.find(dom::propertyNames[i]) != i) {
            return false;
        }
    }
    for (std::size_t i = 0; i < dom::keywordNames.size(); ++i) {
        if (dom::keywordTable.find(dom::keywordNames[i]) != i) {
            return false;
        }
    }
    for (std::size_t i = 0; i < dom::pseudoClassNames.size(); ++i) {
        if (dom::pseudoTable.find(dom::pseudoClassNames[i]) != i) {
            return false;
        }
    }
    return true;
}

static_assert(tableInjective());
static_assert(dom::propertyTable.find("Background-COLOR") == std::nullopt);
static_assert(dom::propertyTable.find("BORDER-RADIUS") ==
              static_cast<std::uint16_t>(Property::borderRadius));
static_assert(dom::keywordTable.find("Space-Between") ==
              static_cast<std::uint16_t>(dom::Keyword::spaceBetween));
static_assert(!dom::propertyTable.find("colour") && !dom::keywordTable.find(""));
static_assert(dom::asciiLower('Z') == 'z' && dom::asciiLower('[') == '[' &&
              dom::asciiLower('@') == '@' && dom::asciiLower('a') == 'a');

bool checkBloom() {
    test::Pcg random(23);
    std::size_t falsePositives = 0;
    std::size_t probes = 0;
    bool noFalseNegatives = true;
    for (int trial = 0; trial < 200; ++trial) {
        dom::Bloom256 filter;
        std::array<std::uint64_t, 24> members{};
        for (auto &member : members) {
            member = random.next();
            filter.add(member);
        }
        for (const auto member : members) {
            dom::Bloom256 mask;
            mask.add(member);
            noFalseNegatives = noFalseNegatives && filter.covers(mask);
        }
        for (int probe = 0; probe < 2000; ++probe) {
            dom::Bloom256 mask;
            mask.add(random.next());
            falsePositives += filter.covers(mask) ? 1 : 0;
            ++probes;
        }
    }
    const double rate = static_cast<double>(falsePositives) / static_cast<double>(probes);
    std::cout << "Bloom false-positive rate at n = 24: " << rate << '\n';
    return noFalseNegatives && rate < 1e-2;
}

bool checkSpecificityLattice() {
    test::Pcg random(29);
    const auto draw = [&] {
        return dom::Specificity::of(random.next() % 8 == 0,
                                    static_cast<std::uint32_t>(random.next() % 3),
                                    static_cast<std::uint32_t>(random.next() % 3),
                                    static_cast<std::uint32_t>(random.next() % 3),
                                    static_cast<std::uint32_t>(random.next() % 3));
    };
    const auto lexicographic = [](const dom::Specificity &s) { return s.packed; };
    for (int i = 0; i < 20000; ++i) {
        const auto a = draw();
        const auto b = draw();
        const auto c = draw();
        if ((a < b && b < c && !(a < c)) || (a < b && b < a) ||
            ((a < b) != (lexicographic(a) < lexicographic(b)))) {
            return false;
        }
    }
    return dom::Specificity::of(false, 1, 0, 0, 0) >
               dom::Specificity::of(false, 0, 999, 999, 999) &&
           dom::Specificity::of(true, 0, 0, 0, 0) >
               dom::Specificity::of(false, 999, 999, 999, 999) &&
           dom::Specificity::of(false, 0, 1, 0, 0) > dom::Specificity::of(false, 0, 0, 999, 999);
}

constexpr std::string_view sheetSource = R"(
:root { --accent: #3a8ee6; --pad: 12px; }
Panel { background: #202020; padding: var(--pad); color: #eeeeee; transition: background spring(18), scale spring(30); }
.card { background: #303030; border-radius: 8px; }
#master { background: var(--accent); }
Panel > Knob { width: 48px; height: 48px; }
Editor .card Knob.big { width: 64px; }
Knob:hover { scale: 1.1; }
Knob[bypass == true] { opacity: 0.5; }
Knob[role="meter"] { color: #ff0000; }
@media (min-width: 900px) and (min-height: 100px) { Panel { gap: 16px; display: grid; } }
Label { font-size: 1.5em; line-height: 1.2em; }
)";

struct Scene {
    dom::DomDocument document;
    dom::NodeId editor, master, big, meter, plain, other, label;

    Scene() {
        editor = document.create("Editor");
        static_cast<void>(document.setAttribute(editor, "style", "font-size: 20px"));
        master = document.create("Panel", editor);
        static_cast<void>(document.setElementId(master, "master"));
        static_cast<void>(document.addClass(master, "card"));
        big = document.create("Knob", master);
        static_cast<void>(document.addClass(big, "big"));
        document.setPseudo(big, dom::pseudo::hover);
        meter = document.create("Knob", master);
        static_cast<void>(document.setAttribute(meter, "role", "meter"));
        other = document.create("Panel", editor);
        static_cast<void>(document.addClass(other, "card"));
        plain = document.create("Knob", other);
        label = document.create("Label", editor);
    }
};

bool near(float a, float b) { return std::abs(a - b) < 1e-5f; }

bool checkCascade() {
    auto compiled = dom::compileStyleSheet(sheetSource);
    if (!compiled.sheet) {
        std::cerr << "Stylesheet error on line " << compiled.error.line << ": "
                  << compiled.error.message << '\n';
        return false;
    }
    dom::StyleSheet &sheet = *compiled.sheet;
    Scene scene;
    dom::StyleResolver resolver(sheet);
    resolver.setViewport(800, 600);
    resolver.setParameter("bypass", 1);
    resolver.resolve(scene.document);
    const auto &master = resolver.style(scene.master);
    const auto &big = resolver.style(scene.big);
    const auto &meter = resolver.style(scene.meter);
    const auto &plain = resolver.style(scene.plain);
    const auto &label = resolver.style(scene.label);
    const bool values =
        master.colors.background == 0x3A8EE6FF &&
        resolver.style(scene.other).colors.background == 0x303030FF &&
        master.colors.borderRadius == 8 && master.layout.padding.value == 12 &&
        big.layout.width.value == 64 && plain.layout.width.value == 48 &&
        big.colors.color == 0xEEEEEEFF && meter.colors.color == 0xFF0000FF &&
        near(big.transform.scale, 1.1f) && near(plain.transform.scale, 1) &&
        near(big.colors.opacity, 0.5f) && master.layout.gap.value == 0 &&
        master.layout.display == dom::Keyword::flex && label.layout.fontSize == 30 &&
        near(label.layout.lineHeight, 36) && master.motion.count == 2 &&
        master.motion.properties[0] == Property::background && master.motion.omega[1] == 30;
    const auto &origin = resolver.provenance(scene.master, Property::background);
    const auto &inheritedColor = resolver.provenance(scene.plain, Property::color);
    const auto &inlineSize = resolver.provenance(scene.editor, Property::fontSize);
    const bool provenance =
        origin.rule == 3 && sheet.rules[3].line == 5 && inheritedColor.rule == 1 &&
        inheritedColor.inheritedFrom == scene.other.index && inlineSize.rule == dom::inlineRule &&
        resolver.provenance(scene.label, Property::fontSize).rule == 10;
    scene.document.setPseudo(scene.big, 0);
    resolver.restyle(scene.document, scene.big);
    const bool transformOnly =
        resolver.lastDiff(scene.big) == dom::diff::transformOnly && resolver.stats().visited == 1;
    resolver.setParameter("bypass", 0);
    resolver.restyle(scene.document, scene.master);
    const bool colorOnly = resolver.lastDiff(scene.big) == dom::diff::colorOnly &&
                           resolver.lastDiff(scene.meter) == dom::diff::colorOnly &&
                           resolver.lastDiff(scene.master) == dom::diff::none;
    resolver.setViewport(1000, 600);
    const auto added = scene.document.create("Knob", scene.other);
    resolver.resolve(scene.document);
    const bool layout = resolver.lastDiff(scene.master) == dom::diff::layout &&
                        resolver.style(scene.master).layout.gap.value == 16 &&
                        resolver.style(scene.master).layout.display == dom::Keyword::grid &&
                        resolver.lastDiff(added) == dom::diff::structure &&
                        resolver.lastDiff(scene.label) == dom::diff::none;
    sheet.setToken("--accent", *dom::parseValue(Property::background, "#ff8800", sheet));
    resolver.resolve(scene.document);
    const bool token = resolver.style(scene.master).colors.background == 0xFF8800FF &&
                       resolver.lastDiff(scene.master) == dom::diff::colorOnly;
    std::cout << "Cascade values " << values << ", provenance " << provenance << ", diffs "
              << transformOnly << colorOnly << layout << token << '\n';
    return values && provenance && transformOnly && colorOnly && layout && token;
}

bool checkCompileErrors() {
    for (const std::string_view bad :
         {"Panel { colour: red; }", "Panel { width: 12furlongs; }", "Panel { background: #12345; }",
          "Panel:sleepy { color: #fff; }", "Panel > { color: #fff; }",
          "@media (min-width: 9em) { A { gap: 1px; } }", "Panel { color: #fff;",
          "Panel { transition: color spring(-1); }", "Panel { --token: 1px; }"}) {
        if (dom::compileStyleSheet(bad).sheet) {
            std::cerr << "Accepted malformed stylesheet: " << bad << '\n';
            return false;
        }
    }
    const auto located = dom::compileStyleSheet("A { gap: 1px; }\n\nB { gap: nope; }");
    return !located.sheet && located.error.line == 3;
}

bool checkAncestorFiltering() {
    std::string source;
    for (int i = 0; i < 200; ++i) {
        source += ".absent" + std::to_string(i) + " .leaf { gap: " + std::to_string(i) + "px; }\n";
    }
    source += ".level3 .leaf { width: 10px; }\n";
    auto compiled = dom::compileStyleSheet(source);
    if (!compiled.sheet) {
        return false;
    }
    dom::DomDocument document;
    auto node = document.create("Root");
    for (int depth = 0; depth < 22; ++depth) {
        node = document.create("Box", node);
        static_cast<void>(document.addClass(node, "level" + std::to_string(depth)));
    }
    const auto leaf = document.create("Box", node);
    static_cast<void>(document.addClass(leaf, "leaf"));
    dom::StyleResolver resolver(*compiled.sheet);
    resolver.resolve(document);
    const auto &stats = resolver.stats();
    const double rejection =
        static_cast<double>(stats.bloomRejected) / static_cast<double>(stats.ancestorChecks);
    const double falsePositive =
        static_cast<double>(stats.bloomFalsePositives) / static_cast<double>(stats.ancestorChecks);
    std::cout << "Ancestor checks " << stats.ancestorChecks << ", Bloom rejected " << rejection
              << ", false positives " << falsePositive << '\n';
    return resolver.style(leaf).layout.width.value == 10 && rejection > 0.99 &&
           falsePositive < 1e-2;
}
}

int main() {
    if (!checkBloom() || !checkSpecificityLattice() || !checkCascade() || !checkCompileErrors() ||
        !checkAncestorFiltering()) {
        std::cerr << "Style cascade contract failed\n";
        return 1;
    }
    return 0;
}
