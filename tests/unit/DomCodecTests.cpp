#include "../support/Pcg.h"
#include "dom/DomCodec.h"
#include "dom/PrefixVarint.h"

#include <iostream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

namespace {
using namespace prism;

static_assert(std::is_same_v<dom::SmallVector<std::uint32_t, 8>::size_type, std::uint8_t>);
static_assert(std::is_same_v<dom::SmallVector<std::uint8_t, 300>::size_type, std::uint16_t>);
static_assert(sizeof(dom::SmallVector<std::uint32_t, 8>) == 36);
static_assert(std::is_trivially_destructible_v<dom::SmallVector<std::uint32_t, 8>>);
static_assert(dom::crc32cChecksum(std::array<std::uint8_t, 9>{'1', '2', '3', '4', '5', '6', '7',
                                                              '8', '9'}) == 0xE3069283U);
static_assert(dom::zigzagEncode(std::numeric_limits<std::int64_t>::min()) ==
              std::numeric_limits<std::uint64_t>::max());

bool roundTripVarint(std::uint64_t value, std::size_t expectedWidth) {
    std::vector<std::uint8_t> bytes;
    dom::writeVarint(bytes, value);
    dom::writeVarint(bytes, value ^ 0x55);
    dom::VarintReader reader(bytes);
    const auto first = reader.read();
    const auto second = reader.read();
    return bytes.size() - dom::varintWidth(value ^ 0x55) == expectedWidth && first == value &&
           second == (value ^ 0x55) && !reader.read();
}

bool checkVarints() {
    bool ok = roundTripVarint(0, 1);
    for (std::size_t k = 1; k <= 8; ++k) {
        const std::uint64_t limit = std::uint64_t{1} << (7 * k);
        ok = ok && roundTripVarint(limit - 1, k) && roundTripVarint(limit, k == 8 ? 9 : k + 1);
    }
    ok = ok && roundTripVarint(std::numeric_limits<std::uint64_t>::max(), 9);
    test::Pcg random(5);
    for (int i = 0; i < 100000 && ok; ++i) {
        const std::uint64_t value = random.next() >> (random.next() % 64);
        ok = roundTripVarint(value, dom::varintWidth(value));
        const auto signedValue = static_cast<std::int64_t>(random.next());
        ok = ok && dom::zigzagDecode(dom::zigzagEncode(signedValue)) == signedValue;
    }
    for (std::int64_t s = -64; s < 64; ++s) {
        ok = ok && dom::varintWidth(dom::zigzagEncode(s)) == 1 &&
             dom::zigzagDecode(dom::zigzagEncode(s)) == s;
    }
    const std::uint8_t truncated[] = {0x04};
    dom::VarintReader reader(truncated);
    return ok && !reader.read() &&
           dom::zigzagDecode(dom::zigzagEncode(std::numeric_limits<std::int64_t>::min())) ==
               std::numeric_limits<std::int64_t>::min();
}

dom::DomDocument sampleDocument() {
    dom::DomDocument document;
    const auto root = document.create("Editor");
    static_cast<void>(
        document.setAttribute(root, "title", "Prism <Synth> & \"Co\"\nline\tend \xE2\x9C\x93"));
    for (int section = 0; section < 4; ++section) {
        const auto panel = document.create("Panel", root);
        static_cast<void>(document.setElementId(panel, "panel-" + std::to_string(section)));
        static_cast<void>(document.addClass(panel, "card"));
        static_cast<void>(document.addClass(panel, section % 2 == 0 ? "accent" : "muted"));
        for (int k = 0; k < 3; ++k) {
            const auto knob = document.create(k == 2 ? "Meter" : "Knob", panel);
            static_cast<void>(document.addClass(knob, "control"));
            static_cast<void>(document.setAttribute(knob, "min", std::to_string(-k * 12)));
            static_cast<void>(document.addBinding(
                knob, k == 2 ? dom::BindingKind::telemetry : dom::BindingKind::parameter,
                "gain" + std::to_string(section * 3 + k), "value"));
            document.setPseudo(knob, static_cast<std::uint8_t>(
                                         k == 0 ? dom::pseudo::hover | dom::pseudo::modulated : 0));
        }
    }
    return document;
}

bool checkRoundTrips() {
    const dom::DomDocument document = sampleDocument();
    const auto binary = dom::encodeBinary(document);
    const auto xml = dom::encodeXml(document);
    const auto json = dom::encodeJson(document);
    const auto fromBinary = dom::decodeBinary(binary);
    const auto fromXml = dom::decodeXml(xml);
    const auto fromJson = dom::decodeJson(json);
    if (!fromBinary || !fromXml || !fromJson) {
        return false;
    }
    const auto chained =
        dom::decodeJson(dom::encodeJson(*dom::decodeBinary(dom::encodeBinary(*fromXml))));
    std::cout << "Binary " << binary.size() << " bytes, XML " << xml.size() << " bytes, JSON "
              << json.size() << " bytes\n";
    return document.equivalent(*fromBinary) && document.equivalent(*fromXml) &&
           document.equivalent(*fromJson) && chained && document.equivalent(*chained) &&
           binary.size() < xml.size() / 2;
}

bool checkForwardCompatibility() {
    std::vector<std::uint8_t> bytes{'P', 'R', 'B', 'N'};
    dom::writeVarint(bytes, 7);
    dom::writeVarint(bytes, 2);
    dom::writeVarint(bytes, 4);
    bytes.insert(bytes.end(), {'R', 'o', 'o', 't'});
    dom::writeVarint(bytes, 1);
    std::vector<std::uint8_t> record;
    for (const std::uint64_t field : {1, 0, 0, 0, 0, 0, 0}) {
        dom::writeVarint(record, field);
    }
    dom::writeVarint(record, 123456789);
    dom::writeVarint(record, std::numeric_limits<std::uint64_t>::max());
    dom::writeVarint(bytes, record.size());
    bytes.insert(bytes.end(), record.begin(), record.end());
    const std::uint32_t checksum = dom::crc32cChecksum(bytes);
    for (int i = 0; i < 4; ++i) {
        bytes.push_back(static_cast<std::uint8_t>(checksum >> (8 * i)));
    }
    bytes.insert(bytes.end(), 8, 0);
    const auto decoded = dom::decodeBinary(bytes);
    return decoded && decoded->size() == 1 &&
           decoded->typeName(decoded->node(decoded->root())) == "Root";
}

bool checkBinaryRejection() {
    const auto binary = dom::encodeBinary(sampleDocument());
    for (std::size_t length = 0; length < binary.size(); ++length) {
        if (dom::decodeBinary(std::span(binary).first(length))) {
            return false;
        }
    }
    auto corrupted = binary;
    for (std::size_t bit = 0; bit < corrupted.size() * 8; ++bit) {
        corrupted[bit / 8] ^= static_cast<std::uint8_t>(1U << (bit % 8));
        if (dom::decodeBinary(corrupted)) {
            return false;
        }
        corrupted[bit / 8] ^= static_cast<std::uint8_t>(1U << (bit % 8));
    }
    return true;
}

bool checkMalformedText() {
    const std::vector<std::string> xml{"",
                                       "<A>",
                                       "<A></B>",
                                       "<A x=1/>",
                                       "<A x=\"1\" x=\"2\"/>",
                                       "<A state=\"sleepy\"/>",
                                       "<A x=\"&bogus;\"/>",
                                       "<A/><B/>",
                                       "<A>text</A>",
                                       "<!-- open <A/>",
                                       "<A bind=\"parameter-gain\"/>",
                                       "<A x=\"&#0;\"/>",
                                       "<A x=\"a<b\"/>",
                                       "<1A/>",
                                       std::string(300, '<'),
                                       [] {
                                           std::string nested;
                                           for (int i = 0; i < 300; ++i) {
                                               nested += "<N>";
                                           }
                                           for (int i = 0; i < 300; ++i) {
                                               nested += "</N>";
                                           }
                                           return nested;
                                       }()};
    for (const auto &sample : xml) {
        if (dom::decodeXml(sample)) {
            std::cerr << "Accepted malformed XML: " << sample.substr(0, 40) << '\n';
            return false;
        }
    }
    const std::vector<std::string> json{
        "",
        "{",
        "{\"type\":\"A\",}",
        "{\"type\":\"A\",\"x\":01}",
        "{\"type\":\"A\"} {}",
        "{\"type\":\"A\",\"type\":\"B\"}",
        "{\"type\":\"A\",\"id\":\"\x01\"}",
        "{\"type\":\"A\",\"id\":\"\\ud800\"}",
        "{\"id\":\"x\"}",
        "{\"type\":\"A\",\"state\":[\"sleepy\"]}",
        "{\"type\":\"A\",\"children\":{}}",
        "{\"type\":\"A\",\"x\":1.}",
        "{\"type\":\"A\",\"x\":tru}",
        std::string(1000, '['),
    };
    for (const auto &sample : json) {
        if (dom::decodeJson(sample)) {
            std::cerr << "Accepted malformed JSON: " << sample.substr(0, 40) << '\n';
            return false;
        }
    }
    return true;
}

template <class Decode, class Encode>
bool fuzz(const std::string &seed, Decode decode, Encode encode, std::uint64_t salt) {
    test::Pcg random(salt);
    std::size_t accepted = 0;
    for (int i = 0; i < 4000; ++i) {
        std::string sample = seed;
        const int edits = 1 + static_cast<int>(random.next() % 4);
        for (int e = 0; e < edits && !sample.empty(); ++e) {
            const std::size_t at = random.next() % sample.size();
            switch (random.next() % 3) {
            case 0:
                sample[at] = static_cast<char>(random.next());
                break;
            case 1:
                sample.erase(at, 1 + random.next() % 8);
                break;
            default:
                sample.insert(at, 1, "<>\"'&;{}[],:\\/"[random.next() % 14]);
            }
        }
        const auto decoded = decode(sample);
        if (decoded) {
            ++accepted;
            const auto again = decode(encode(*decoded));
            if (!again || !again->equivalent(*decoded)) {
                return false;
            }
        }
    }
    std::cout << "Fuzz accepted " << accepted << " of 4000 mutations\n";
    return true;
}

bool checkReconciliation() {
    dom::DomDocument live = sampleDocument();
    dom::DomDocument next = sampleDocument();
    std::vector<dom::NodeId> panels;
    live.forEachChild(live.root(), [&](dom::NodeId child) { panels.push_back(child); });
    std::vector<dom::NodeId> nextPanels;
    next.forEachChild(next.root(), [&](dom::NodeId child) { nextPanels.push_back(child); });
    static_cast<void>(next.setAttribute(nextPanels[1], "collapsed", "true"));
    next.remove(nextPanels[2]);
    next.moveToEnd(nextPanels[0]);
    const auto added = next.create("Button", nextPanels[3]);
    static_cast<void>(next.addClass(added, "primary"));
    const auto stats = dom::reconcile(live, next);
    std::cout << "Reconcile created " << stats.created << ", removed " << stats.removed
              << ", updated " << stats.updated << ", kept " << stats.kept << '\n';
    return live.equivalent(next) && stats.created == 1 && stats.removed == 4 &&
           stats.updated == 1 && live.contains(panels[0]) && live.contains(panels[1]) &&
           !live.contains(panels[2]) && live.contains(panels[3]);
}
}

int main() {
    const auto seed = sampleDocument();
    const bool fuzzed =
        fuzz(
            dom::encodeXml(seed), [](const std::string &s) { return dom::decodeXml(s); },
            [](const dom::DomDocument &d) { return dom::encodeXml(d); }, 17) &&
        fuzz(
            dom::encodeJson(seed), [](const std::string &s) { return dom::decodeJson(s); },
            [](const dom::DomDocument &d) { return dom::encodeJson(d); }, 19);
    if (!checkVarints() || !checkRoundTrips() || !checkForwardCompatibility() ||
        !checkBinaryRejection() || !checkMalformedText() || !fuzzed || !checkReconciliation()) {
        std::cerr << "DOM codec contract failed\n";
        return 1;
    }
    return 0;
}
