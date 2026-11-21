#include "../support/AllocGuard.h"
#include "backends/cpu/CpuRefBackend.h"
#include "widgets/Button.h"
#include "widgets/ComboBox.h"
#include "widgets/GpuScope.h"
#include "widgets/GpuSpectrum.h"
#include "widgets/NodeCard.h"
#include "widgets/RotaryKnob.h"
#include "widgets/Slider.h"
#include "widgets/VerletCableLayer.h"
#include "widgets/XYPad.h"

#include <array>
#include <cmath>
#include <iostream>
#include <new>
#include <numbers>
#include <string>
#include <vector>

namespace {
using namespace prism;
using widgets::PointerEvent;
using widgets::PointerKind;

struct Sink final : host::HostParameterSink {
    std::vector<std::string> events;
    bool record = true;
    void beginGesture(std::uint32_t index) override {
        if (record) {
            events.push_back("b" + std::to_string(index));
        }
    }
    void setValueNotifyingHost(std::uint32_t index, float) override {
        if (record) {
            events.push_back("s" + std::to_string(index));
        }
    }
    void endGesture(std::uint32_t index) override {
        if (record) {
            events.push_back("e" + std::to_string(index));
        }
    }
};

std::vector<host::ParameterSpec> specs() {
    return {{"cutoff", "Cutoff", 0, 1, 0.5f, 0, ""}, {"x", "X", 0, 1, 0.25f, 0, ""},
            {"y", "Y", 0, 1, 0.75f, 0, ""},          {"mute", "Mute", 0, 1, 0, 1, ""},
            {"mode", "Mode", 0, 3, 0, 1, ""},        {"gain", "Gain", 0, 1, 0, 0, ""}};
}

const auto text = [](render::DrawList &list, std::string_view, math::Vector2 at,
                     std::uint32_t color) {
    const float x = static_cast<float>(at.x);
    const float y = static_cast<float>(at.y);
    list.roundedRect({x - 4, y - 3, x + 4, y + 3}, {}, color);
};

bool near(float a, float b) { return std::abs(a - b) < 1e-5f; }

bool checkKnob() {
    host::ParameterMirror mirror(specs());
    Sink sink;
    widgets::RotaryKnob knob({mirror, sink, 0}, {50, 50}, 16);
    const bool missed = !knob.handle({PointerKind::down, {90, 90}});
    knob.handle({PointerKind::down, {50, 50}});
    knob.handle({PointerKind::move, {50, 10}});
    const float coarse = mirror.normalized(0);
    knob.handle({PointerKind::move, {50, 10}, 0, true});
    const float fine = mirror.normalized(0);
    knob.handle({PointerKind::up, {50, 10}});
    const bool bracketed =
        sink.events.front() == "b0" && sink.events.back() == "e0" && sink.events.size() == 4;
    knob.handle({PointerKind::wheel, {50, 50}, 3});
    const float wheeled = mirror.normalized(0);
    knob.handle({PointerKind::doubleClick, {50, 50}});
    return missed && near(coarse, 0.7f) && near(fine, 0.52f) && bracketed && near(wheeled, 0.55f) &&
           near(mirror.normalized(0), 0.5f) && near(knob.angleDegrees(), 0);
}

bool checkSlider() {
    host::ParameterMirror mirror(specs());
    Sink sink;
    widgets::Slider slider({mirror, sink, 0}, {0, 0, 110, 20}, false);
    const math::Vector2 thumb = slider.thumb();
    slider.handle({PointerKind::down, {thumb.x + 3, 10}});
    const bool noJump = near(mirror.normalized(0), 0.5f);
    slider.handle({PointerKind::move, {thumb.x + 3 + 25, 10}});
    const bool relative = near(mirror.normalized(0), 0.75f);
    slider.handle({PointerKind::up, {0, 0}});
    slider.handle({PointerKind::down, {5, 10}});
    slider.handle({PointerKind::up, {5, 10}});
    const bool jumped = near(mirror.normalized(0), 0);
    widgets::Slider fader({mirror, sink, 5}, {0, 0, 22, 110}, true);
    fader.handle({PointerKind::down, {11, 5}});
    fader.handle({PointerKind::up, {11, 5}});
    return noJump && relative && jumped && near(mirror.normalized(5), 1);
}

bool checkPadButtonCombo() {
    host::ParameterMirror mirror(specs());
    Sink sink;
    widgets::XYPad pad({mirror, sink, 1}, {mirror, sink, 2}, {0, 0, 100, 100});
    sink.events.clear();
    pad.handle({PointerKind::down, {20, 30}});
    pad.handle({PointerKind::up, {20, 30}});
    const bool padOk = near(mirror.normalized(1), 0.2f) && near(mirror.normalized(2), 0.7f) &&
                       sink.events == std::vector<std::string>{"b1", "b2", "s1", "s2", "e1", "e2"};
    widgets::Button toggle({mirror, sink, 3}, {0, 0, 40, 20}, widgets::ButtonMode::toggle, "MUTE");
    toggle.handle({PointerKind::down, {5, 5}});
    toggle.handle({PointerKind::up, {5, 5}});
    const bool toggled = toggle.on();
    widgets::Button momentary({mirror, sink, 5}, {0, 0, 40, 20}, widgets::ButtonMode::momentary,
                              "TAP");
    momentary.handle({PointerKind::down, {5, 5}});
    const bool held = near(mirror.normalized(5), 1);
    momentary.handle({PointerKind::up, {5, 5}});
    const std::array<std::string_view, 4> items{"Sine", "Saw", "Square", "Noise"};
    widgets::ComboBox combo({mirror, sink, 4}, {0, 0, 80, 20}, items);
    combo.handle({PointerKind::down, {10, 10}});
    const bool opened = combo.open();
    combo.handle({PointerKind::move, {10, 22 + 20 * 2 + 5}});
    combo.handle({PointerKind::down, {10, 22 + 20 * 2 + 5}});
    const bool picked = !combo.open() && combo.selected() == 2 && near(mirror.plain(4), 2);
    combo.handle({PointerKind::wheel, {10, 10}, -1});
    render::DrawList list;
    combo.handle({PointerKind::down, {10, 10}});
    combo.paint(list, {}, text);
    return padOk && toggled && held && near(mirror.normalized(5), 0) && opened && picked &&
           combo.selected() == 3 && list.primitives().size() > 8;
}

std::array<math::Vector2, widgets::cablePoints> referenceCable(math::Vector2 a, math::Vector2 b,
                                                               double rest,
                                                               const widgets::CableSettings &s,
                                                               int steps, double dt) {
    constexpr std::size_t n = widgets::cablePoints;
    std::array<math::Vector2, n> x{};
    for (std::size_t i = 0; i < n; ++i) {
        x[i] = a + (b - a) * (static_cast<double>(i) / (n - 1));
    }
    auto previous = x;
    const double segment = rest / (n - 1);
    for (int step = 0; step < steps; ++step) {
        x.front() = a;
        x.back() = b;
        for (std::size_t i = 1; i + 1 < n; ++i) {
            const math::Vector2 v = (x[i] - previous[i]) * s.damping;
            previous[i] = x[i];
            x[i] = x[i] + v + s.gravity * (dt * dt);
        }
        for (int k = 0; k < s.iterations; ++k) {
            for (std::size_t j = 0; j + 1 < n; ++j) {
                const std::size_t i = k % 2 == 0 ? j : n - 2 - j;
                const math::Vector2 d = x[i + 1] - x[i];
                const double distance = math::length(d);
                if (!(distance > segment)) {
                    continue;
                }
                const math::Vector2 c = d * ((distance - segment) / distance);
                if (i == 0) {
                    x[i + 1] = x[i + 1] - c;
                } else if (i + 2 == n) {
                    x[i] = x[i] + c;
                } else {
                    x[i] = x[i] + c * 0.5;
                    x[i + 1] = x[i + 1] - c * 0.5;
                }
            }
        }
    }
    return x;
}

bool checkCards() {
    const std::array<widgets::Port, 2> ports{{{{150, 40}, true}, {{0, 40}, false}}};
    widgets::NodeCard a({20, 20}, {150, 90}, ports, "OSC");
    widgets::NodeCard b({300, 60}, {150, 90}, ports, "FILTER");
    widgets::VerletCableLayer layer(8);
    const auto cable = layer.connect(a, 0, b, 1);
    const widgets::CableSettings settings;
    const double rest = math::length(b.portPosition(1) - a.portPosition(0)) * settings.slack + 1;
    for (int i = 0; i < 600; ++i) {
        layer.step(1.0 / 120);
    }
    const auto expected =
        referenceCable(a.portPosition(0), b.portPosition(1), rest, settings, 600, 1.0 / 120);
    double worst = 0;
    const auto points = layer.points(*cable);
    for (std::size_t i = 0; i < points.size(); ++i) {
        worst = std::max(worst, math::length(points[i] - expected[i]));
    }
    double stretch = 0;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        stretch = std::max(stretch,
                           math::length(points[i + 1] - points[i]) / (rest / (points.size() - 1)));
    }
    const math::Vector2 chordMid = (points.front() + points.back()) * 0.5;
    const bool sags = points[points.size() / 2].y > chordMid.y + 20;
    a.handle({PointerKind::down, {60, 30}});
    a.handle({PointerKind::move, {80, 130}});
    a.handle({PointerKind::up, {80, 130}});
    layer.step(1.0 / 120);
    const bool follows = math::length(layer.points(*cable).front() - a.portPosition(0)) < 1e-9 &&
                         std::abs(a.position().y - 120) < 1e-9 && a.portAt(a.portPosition(0)) == 0;
    std::cout << "Verlet deviation " << worst << " px, stretch " << stretch << '\n';
    return cable && worst <= 1e-6 && stretch < 1.025 && sags && follows;
}

bool checkSteadyState() {
    host::ParameterMirror mirror(specs());
    Sink sink;
    sink.record = false;
    std::vector<widgets::RotaryKnob> knobs;
    for (int i = 0; i < 8; ++i) {
        knobs.emplace_back(widgets::BoundParameter{mirror, sink, static_cast<std::uint32_t>(i % 6)},
                           math::Vector2{30.0 + 40 * i, 30}, 16);
    }
    widgets::Slider slider({mirror, sink, 5}, {10, 70, 210, 90}, false);
    widgets::XYPad pad({mirror, sink, 1}, {mirror, sink, 2}, {230, 60, 330, 160});
    const std::array<widgets::Port, 2> ports{{{{120, 30}, true}, {{0, 30}, false}}};
    widgets::NodeCard a({10, 110}, {120, 60}, ports, "A");
    widgets::NodeCard b({180, 170}, {120, 60}, ports, "B");
    widgets::VerletCableLayer cables(4);
    static_cast<void>(cables.connect(a, 0, b, 1));
    widgets::GpuScope scope(128, 1 << 14);
    widgets::GpuSpectrum spectrum(128, 48000);
    std::vector<float> block(widgets::spectrumSize);
    render::DrawList list;
    backends::CpuRefSurface surface({{96, 64}});
    surface.reserveFrame(512, 512 * 24);
    const hal::DamageRegion damage({0, 0, 96, 64});
    std::size_t allocations = 0;
    bool rendered = true;
    const auto frameBody = [&](int frame) {
        const auto t = static_cast<float>(frame);
        for (std::size_t n = 0; n < block.size(); ++n) {
            block[n] = std::sin(0.05f * static_cast<float>(n) + t * 0.1f);
        }
        mirror.hostSetNormalized(static_cast<std::uint32_t>(frame % 6),
                                 0.5f + 0.5f * std::sin(t * 0.01f));
        mirror.consumeChanges([](std::uint32_t, float) {});
        scope.append(std::span(block).first(256));
        scope.build(1);
        if (frame % 50 == 0) {
            spectrum.analyze(block, 1.0 / 60);
            spectrum.buildStrip(-90, 0);
        }
        a.handle({PointerKind::down, {20, 115}});
        a.handle({PointerKind::move, {20 + 10 * std::sin(t * 0.02f), 115}});
        a.handle({PointerKind::up, {20, 115}});
        cables.step(1.0 / 120);
        list.clear();
        for (const auto &knob : knobs) {
            knob.paint(list, {});
        }
        slider.paint(list, {});
        pad.paint(list, {});
        a.paint(list, {}, text);
        b.paint(list, {}, text);
        cables.paint(list, render::packColor(240, 140, 80, 255), 3);
        const std::array<render::TextureView, 1> textures{scope.strip()};
        const render::FrameView view{
            list.primitives(), list.payload(), list.transforms(),  list.clips(), {},
            textures,          list.paths(),   list.pathSegments()};
        rendered = rendered && surface.render({view, &damage, {0, 0, 0, 1}});
    };
    frameBody(0);
    for (int frame = 0; frame < 10000; ++frame) {
        try {
            test::AllocGuard guard;
            frameBody(frame);
        } catch (const std::bad_alloc &) {
            ++allocations;
        }
    }
    std::cout << "Steady-state allocations over 10,000 frames: " << allocations << '\n';
    return allocations == 0 && rendered;
}
}

int main() {
    if (!checkKnob() || !checkSlider() || !checkPadButtonCombo() || !checkCards() ||
        !checkSteadyState()) {
        std::cerr << "Widget interaction contract failed\n";
        return 1;
    }
    return 0;
}
