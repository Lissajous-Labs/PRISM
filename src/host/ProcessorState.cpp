#include "host/ProcessorState.h"

#include "dom/DomCodec.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace prism::host {
ParameterMirror::ParameterMirror(std::vector<ParameterSpec> specs)
    : specs_(std::move(specs)), values_(std::make_unique<std::atomic<float>[]>(specs_.size())),
      gestures_(std::make_unique<std::atomic<bool>[]>(specs_.size())),
      dirty_((specs_.size() + 63) / 64) {
    for (std::size_t i = 0; i < specs_.size(); ++i) {
        values_[i].store(toNormalized(static_cast<std::uint32_t>(i), specs_[i].defaultValue),
                         std::memory_order_relaxed);
        gestures_[i].store(false, std::memory_order_relaxed);
    }
    for (auto &word : dirty_) {
        word.store(0, std::memory_order_relaxed);
    }
}

void ParameterMirror::markDirty(std::uint32_t index) noexcept PRISM_NONBLOCKING {
    dirty_[index / 64].fetch_or(std::uint64_t{1} << (index % 64), std::memory_order_acq_rel);
}

float ParameterMirror::quantize(std::uint32_t index,
                                float normalized) const noexcept PRISM_NONBLOCKING {
    normalized = std::clamp(normalized, 0.0f, 1.0f);
    const ParameterSpec &spec = specs_[index];
    if (spec.step <= 0 || spec.maximum <= spec.minimum) {
        return normalized;
    }
    const float steps = std::round((spec.maximum - spec.minimum) / spec.step);
    return std::round(normalized * steps) / steps;
}

void ParameterMirror::hostSetNormalized(std::uint32_t index,
                                        float normalized) noexcept PRISM_NONBLOCKING {
    values_[index].store(quantize(index, normalized), std::memory_order_release);
    markDirty(index);
}

float ParameterMirror::toPlain(std::uint32_t index, float normalized) const noexcept {
    const ParameterSpec &spec = specs_[index];
    return spec.minimum + quantize(index, normalized) * (spec.maximum - spec.minimum);
}

float ParameterMirror::toNormalized(std::uint32_t index, float plain) const noexcept {
    const ParameterSpec &spec = specs_[index];
    if (spec.maximum <= spec.minimum) {
        return 0;
    }
    return quantize(index, (plain - spec.minimum) / (spec.maximum - spec.minimum));
}

std::string ParameterMirror::format(std::uint32_t index) const {
    const ParameterSpec &spec = specs_[index];
    char buffer[64];
    const int decimals = spec.step >= 1 ? 0 : 2;
    const float resolution = decimals == 0 ? 0.5f : 0.005f;
    const float raw = plain(index);
    const float value = std::abs(raw) < resolution ? 0.0f : raw;
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, static_cast<double>(value));
    std::string text = buffer;
    if (!spec.unit.empty()) {
        text += ' ';
        text += spec.unit;
    }
    return text;
}

void ParameterMirror::beginEdit(std::uint32_t index, HostParameterSink &sink) {
    if (!gestures_[index].exchange(true, std::memory_order_acq_rel)) {
        sink.beginGesture(index);
    }
}

void ParameterMirror::edit(std::uint32_t index, float normalized, HostParameterSink &sink) {
    const bool bracketed = gestures_[index].load(std::memory_order_acquire);
    if (!bracketed) {
        beginEdit(index, sink);
    }
    const float value = quantize(index, normalized);
    values_[index].store(value, std::memory_order_release);
    markDirty(index);
    sink.setValueNotifyingHost(index, value);
    if (!bracketed) {
        endEdit(index, sink);
    }
}

void ParameterMirror::endEdit(std::uint32_t index, HostParameterSink &sink) {
    if (gestures_[index].exchange(false, std::memory_order_acq_rel)) {
        sink.endGesture(index);
    }
}

std::optional<std::uint32_t> ParameterMirror::indexOf(std::string_view id) const noexcept {
    for (std::size_t i = 0; i < specs_.size(); ++i) {
        if (specs_[i].id == id) {
            return static_cast<std::uint32_t>(i);
        }
    }
    return std::nullopt;
}

std::vector<std::uint8_t> saveState(const ParameterMirror &mirror) {
    dom::DomDocument document;
    const auto root = document.create("ProcessorState");
    for (std::uint32_t i = 0; i < mirror.size(); ++i) {
        const auto node = document.create("Parameter", root);
        char bits[16];
        const auto result = std::to_chars(bits, bits + sizeof(bits),
                                          std::bit_cast<std::uint32_t>(mirror.normalized(i)), 16);
        static_cast<void>(document.setAttribute(node, "id", mirror.spec(i).id));
        static_cast<void>(document.setAttribute(
            node, "value", std::string_view(bits, static_cast<std::size_t>(result.ptr - bits))));
    }
    return dom::encodeBinary(document);
}

bool loadState(std::span<const std::uint8_t> bytes, ParameterMirror &mirror) {
    const auto document = dom::decodeBinary(bytes);
    if (!document || !document->root().valid() ||
        document->typeName(document->node(document->root())) != "ProcessorState") {
        return false;
    }
    bool ok = true;
    document->forEachChild(document->root(), [&](dom::NodeId child) {
        const auto id = document->attribute(child, "id");
        const auto value = document->attribute(child, "value");
        if (!id || !value) {
            ok = false;
            return;
        }
        std::uint32_t bits = 0;
        const auto parsed = std::from_chars(value->data(), value->data() + value->size(), bits, 16);
        const auto index = mirror.indexOf(*id);
        if (parsed.ec != std::errc{} || !index) {
            return;
        }
        mirror.hostSetNormalized(*index, std::bit_cast<float>(bits));
    });
    return ok;
}
}
