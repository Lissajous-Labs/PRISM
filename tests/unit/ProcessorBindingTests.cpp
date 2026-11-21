#include "../support/ReferenceScene.h"
#include "backends/cpu/CpuRefBackend.h"
#include "host/AccessibilityBridge.h"
#include "host/ComponentProxy.h"
#include "host/ProcessorState.h"
#include "host/SurfaceLifecycle.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace prism;

struct RecordingSink final : host::HostParameterSink {
    std::vector<std::string> events;
    void beginGesture(std::uint32_t index) override {
        events.push_back("begin" + std::to_string(index));
    }
    void setValueNotifyingHost(std::uint32_t index, float value) override {
        events.push_back("set" + std::to_string(index) + ":" +
                         std::to_string(static_cast<int>(std::lround(value * 100))));
    }
    void endGesture(std::uint32_t index) override {
        events.push_back("end" + std::to_string(index));
    }
};

std::vector<host::ParameterSpec> specs() {
    return {{"gain", "Gain", -60, 12, 0, 0, "dB"},
            {"bypass", "Bypass", 0, 1, 0, 1, ""},
            {"mode", "Mode", 0, 10, 0, 1, ""},
            {"cutoff", "Cutoff", 20, 20000, 1000, 0, "Hz"}};
}

bool checkGestures() {
    host::ParameterMirror mirror(specs());
    RecordingSink sink;
    mirror.beginEdit(0, sink);
    mirror.beginEdit(0, sink);
    mirror.edit(0, 0.3f, sink);
    mirror.edit(0, 0.6f, sink);
    mirror.endEdit(0, sink);
    mirror.endEdit(0, sink);
    mirror.edit(2, 0.33f, sink);
    const std::vector<std::string> expected{"begin0", "set0:30", "set0:60", "end0",
                                            "begin2", "set2:30", "end2"};
    std::vector<std::pair<std::uint32_t, float>> changes;
    mirror.hostSetNormalized(1, 1);
    mirror.consumeChanges(
        [&](std::uint32_t index, float value) { changes.emplace_back(index, value); });
    const bool drained = mirror.consumeChanges([](std::uint32_t, float) {}) == 0;
    auto compiled = dom::compileStyleSheet("Knob[bypass == 1] { opacity: 0.25; }");
    dom::DomDocument document;
    const auto knob = document.create("Knob");
    dom::StyleResolver resolver(*compiled.sheet);
    for (const auto &[index, value] : changes) {
        resolver.setParameter(mirror.spec(index).id, mirror.toPlain(index, value));
    }
    resolver.resolve(document);
    return sink.events == expected && changes.size() == 3 && changes[0].first == 0 &&
           changes[1].first == 1 && std::abs(mirror.plain(2) - 3) < 1e-6f && drained &&
           resolver.style(knob).colors.opacity == 0.25f && mirror.format(0) == "-16.80 dB";
}

bool checkMidiLearn() {
    host::ParameterMirror mirror(specs());
    host::MidiLearnMapper learn;
    const bool ignored = !learn.handleController(0, 74, 10, mirror);
    learn.arm(3);
    const bool bound = learn.handleController(1, 74, 127, mirror) && learn.binding(1, 74) == 3 &&
                       mirror.normalized(3) == 1;
    const bool follows = learn.handleController(1, 74, 0, mirror) && mirror.normalized(3) == 0;
    const bool otherIgnored =
        !learn.handleController(1, 75, 64, mirror) && !learn.handleController(2, 74, 64, mirror);
    learn.unbind(1, 74);
    return ignored && bound && follows && otherIgnored &&
           !learn.handleController(1, 74, 64, mirror);
}

bool checkPlayhead() {
    host::PlayheadSnapshot snapshot;
    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (int k = 1; k <= 200000; ++k) {
            snapshot.publish(
                {static_cast<double>(k), 2.0 * k, k % 7 + 1, 4 << (k % 3), k % 2 == 0, k % 3 == 0});
        }
        done = true;
    });
    bool consistent = true;
    std::size_t reads = 0;
    while (!done.load() || reads < 1000) {
        const host::PlayheadState state = snapshot.read();
        const auto k = static_cast<int>(state.bpm);
        if (state.ppq != 0) {
            consistent = consistent && state.ppq == 2.0 * k && state.numerator == k % 7 + 1 &&
                         state.denominator == (4 << (k % 3)) && state.playing == (k % 2 == 0) &&
                         state.recording == (k % 3 == 0);
        }
        ++reads;
    }
    writer.join();
    return consistent && snapshot.read().bpm == 200000;
}

bool checkPersistence() {
    host::ParameterMirror mirror(specs());
    mirror.hostSetNormalized(0, 0.123456789f);
    mirror.hostSetNormalized(3, 0.7f);
    const auto saved = host::saveState(mirror);
    host::ParameterMirror restored(specs());
    const bool loaded = host::loadState(saved, restored);
    bool identical = true;
    for (std::uint32_t i = 0; i < mirror.size(); ++i) {
        identical = identical && std::bit_cast<std::uint32_t>(mirror.normalized(i)) ==
                                     std::bit_cast<std::uint32_t>(restored.normalized(i));
    }
    auto corrupted = saved;
    corrupted[corrupted.size() / 2] ^= 1;
    return loaded && identical && host::saveState(restored) == saved &&
           !host::loadState(corrupted, restored);
}

bool checkAccessibility() {
    auto compiled = dom::compileStyleSheet(R"(
Editor { display: flex; padding: 4px; gap: 4px; }
Knob { width: 40px; height: 40px; }
Button { width: 60px; height: 20px; }
Toggle { width: 20px; height: 20px; }
ComboBox { width: 80px; height: 20px; }
Label { width: 50px; height: 12px; }
)");
    dom::DomDocument document;
    const auto editor = document.create("Editor");
    static_cast<void>(document.setAttribute(editor, "label", "Prism Synth"));
    const auto knob = document.create("Knob", editor);
    static_cast<void>(document.addBinding(knob, dom::BindingKind::parameter, "gain", "value"));
    const auto button = document.create("Button", editor);
    static_cast<void>(document.setAttribute(button, "label", "Reset"));
    const auto toggle = document.create("Toggle", editor);
    static_cast<void>(document.addBinding(toggle, dom::BindingKind::parameter, "bypass", "value"));
    const auto combo = document.create("ComboBox", editor);
    static_cast<void>(document.setElementId(combo, "mode-menu"));
    const auto label = document.create("Label", editor);
    static_cast<void>(document.setAttribute(label, "label", "Output"));
    dom::StyleResolver resolver(*compiled.sheet);
    resolver.resolve(document);
    dom::layout::LayoutEngine layout;
    layout.layout(document, resolver, {400, 100, 2});
    host::ParameterMirror mirror(specs());
    host::AccessibilityBridge bridge;
    bridge.rebuild(document, layout, mirror);
    const auto &nodes = bridge.nodes();
    const bool tree = nodes.size() == 6 && nodes[1].role == host::AccessibleRole::slider &&
                      nodes[1].label == "Gain" && nodes[1].value == "0.00 dB" &&
                      nodes[1].parent == 0 && nodes[4].label == "mode-menu" &&
                      nodes[1].bounds.x1 - nodes[1].bounds.x0 == 80 && bridge.validate().empty();
    RecordingSink sink;
    const bool incremented = bridge.perform(1, host::actions::increment, mirror, sink) &&
                             sink.events.size() == 3 && sink.events.front() == "begin0" &&
                             nodes[1].value == "0.72 dB";
    const bool toggled =
        bridge.perform(3, host::actions::toggle, mirror, sink) && mirror.normalized(1) == 1;
    const bool refused = !bridge.perform(5, host::actions::press, mirror, sink);
    const auto unlabeled = document.create("Button", editor);
    resolver.resolve(document);
    layout.layout(document, resolver, {400, 100, 2});
    bridge.rebuild(document, layout, mirror);
    const auto issues = bridge.validate();
    static_cast<void>(unlabeled);
    std::cout << "Accessible nodes " << nodes.size() << ", issues after unlabeled button "
              << issues.size() << '\n';
    return tree && incremented && toggled && refused && issues.size() == 1;
}

bool checkProxy() {
    host::ComponentProxy proxy(16, 8);
    proxy.repaint([](std::span<float> texels, std::uint32_t, std::uint32_t) {
        for (std::size_t i = 0; i < texels.size(); i += 4) {
            texels[i] = 0.25f;
            texels[i + 1] = 0.5f;
            texels[i + 2] = 0.75f;
            texels[i + 3] = 1;
        }
    });
    render::DrawList list;
    proxy.place(list, {10, 10, 42, 26}, 0, render::packColor(255, 255, 255, 255));
    const std::array<render::TextureView, 1> textures{proxy.texture()};
    backends::CpuRefSurface surface({{64, 32}});
    const auto pixels = test::renderFull(surface, test::viewOf(list, textures), {0, 0, 0, 1});
    const float *center = &pixels[(18 * 64 + 26) * 4];
    host::ProxyEvent received{};
    const bool inside = proxy.forward({host::ProxyEventKind::mouseDown, 26, 18},
                                      [&](const host::ProxyEvent &e) { received = e; });
    const bool outside =
        !proxy.forward({host::ProxyEventKind::mouseDown, 5, 18}, [](const host::ProxyEvent &) {});
    return std::abs(center[0] - 0.25f) < 1e-3f && std::abs(center[2] - 0.75f) < 1e-3f && inside &&
           outside && received.x == 8 && received.y == 4 && proxy.generation() == 1;
}

bool checkSixteenInstances() {
    constexpr int instances = 16;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    std::array<std::atomic<int>, instances> faults{};
    for (int n = 0; n < instances; ++n) {
        threads.emplace_back([&, n] {
            host::ParameterMirror mirror(specs());
            host::PlayheadSnapshot playhead;
            host::SurfaceLifecycle lifecycle;
            backends::CpuRefSurface surface({{48, 48}});
            lifecycle.handle(host::SurfaceEvent::peerValid);
            lifecycle.handle(host::SurfaceEvent::surfaceOk);
            std::atomic<bool> audioDone{false};
            std::thread audio([&] {
                for (int i = 0; i <= 20000; ++i) {
                    mirror.hostSetNormalized(static_cast<std::uint32_t>(i % 4),
                                             static_cast<float>(i % 1000) / 999.0f);
                    playhead.publish({120.0 + n, static_cast<double>(i), 4, 4, true, false});
                }
                audioDone = true;
            });
            render::DrawList list;
            int frames = 0;
            while (!audioDone.load() || frames < 10) {
                float level = 0;
                mirror.consumeChanges([&](std::uint32_t, float value) { level = value; });
                list.clear();
                list.roundedRect({4, 4, 4 + 40 * level, 20}, {2, 2, 2, 2},
                                 render::packColor(200, 100, 50, 255));
                if (n == 5 && frames == 3) {
                    surface.loseDevice();
                }
                if (surface.lost()) {
                    lifecycle.handle(host::SurfaceEvent::gpuFault);
                    ++faults[static_cast<std::size_t>(n)];
                    if (!surface.rebuild() || !lifecycle.handle(host::SurfaceEvent::rebuilt)) {
                        ++failures;
                    }
                }
                if (!lifecycle.canRender() ||
                    test::renderFull(surface, test::viewOf(list)).empty()) {
                    ++failures;
                }
                ++frames;
            }
            audio.join();
            if (playhead.read().bpm != 120.0 + n ||
                std::abs(mirror.normalized(0) - static_cast<float>(20000 % 1000) / 999.0f) > 0 ||
                lifecycle.state() != host::SurfaceState::surfaceReady) {
                ++failures;
            }
            lifecycle.handle(host::SurfaceEvent::detach);
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }
    bool isolated = true;
    for (int n = 0; n < instances; ++n) {
        isolated = isolated && faults[static_cast<std::size_t>(n)].load() == (n == 5 ? 1 : 0);
    }
    return failures.load() == 0 && isolated;
}
}

int main() {
    if (!checkGestures() || !checkMidiLearn() || !checkPlayhead() || !checkPersistence() ||
        !checkAccessibility() || !checkProxy() || !checkSixteenInstances()) {
        std::cerr << "Processor binding contract failed\n";
        return 1;
    }
    return 0;
}
