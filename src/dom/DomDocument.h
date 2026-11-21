#pragma once

#include "core/FunctionRef.h"
#include "dom/SmallVector.h"

#include <array>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace prism::dom {
using StringId = std::uint32_t;
using TypeId = std::uint16_t;
inline constexpr StringId noString = 0;
inline constexpr std::uint32_t noIndex = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::size_t maxClasses = 8;
inline constexpr std::size_t maxAttributes = 24;
inline constexpr std::size_t maxBindings = 4;

struct NodeId {
    std::uint32_t index = noIndex;
    std::uint32_t generation = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return index != noIndex; }
    friend constexpr bool operator==(const NodeId &, const NodeId &) = default;
};

namespace pseudo {
inline constexpr std::uint8_t hover = 1, active = 2, focus = 4, disabled = 8, checked = 16;
inline constexpr std::uint8_t modulated = 32, selected = 64;
inline constexpr std::array<std::string_view, 7> names{
    "hover", "active", "focus", "disabled", "checked", "modulated", "selected"};
}

enum class BindingKind : std::uint8_t { parameter, property, trigger, telemetry };
inline constexpr std::array<std::string_view, 4> bindingNames{"parameter", "property", "trigger",
                                                              "telemetry"};

struct Binding {
    BindingKind kind;
    StringId source;
    StringId path;
    friend constexpr bool operator==(const Binding &, const Binding &) = default;
};

struct Attribute {
    StringId name;
    StringId value;
    friend constexpr bool operator==(const Attribute &, const Attribute &) = default;
};

struct DomNode {
    std::uint32_t generation = 0;
    TypeId type = 0;
    StringId element = noString;
    std::uint8_t pseudo = 0;
    bool alive = false;
    std::uint32_t parent = noIndex;
    std::uint32_t firstChild = noIndex;
    std::uint32_t lastChild = noIndex;
    std::uint32_t previousSibling = noIndex;
    std::uint32_t nextSibling = noIndex;
    SmallVector<StringId, maxClasses> classes;
    SmallVector<Attribute, maxAttributes> attributes;
    SmallVector<Binding, maxBindings> bindings;
};

class StringPool {
  public:
    StringPool() { static_cast<void>(intern({})); }
    StringPool(StringPool &&) noexcept = default;
    StringPool &operator=(StringPool &&) noexcept = default;
    StringPool(const StringPool &) = delete;
    StringPool &operator=(const StringPool &) = delete;

    [[nodiscard]] StringId intern(std::string_view text);
    [[nodiscard]] std::optional<StringId> find(std::string_view text) const;
    [[nodiscard]] std::string_view view(StringId id) const noexcept { return storage_[id]; }
    [[nodiscard]] std::size_t size() const noexcept { return storage_.size(); }

  private:
    std::deque<std::string> storage_;
    std::unordered_map<std::string_view, StringId> lookup_;
};

class DomDocument {
  public:
    [[nodiscard]] NodeId create(std::string_view type, NodeId parent = {});
    void remove(NodeId id);
    void moveToEnd(NodeId id);

    bool setElementId(NodeId id, std::string_view element);
    bool addClass(NodeId id, std::string_view name);
    bool setAttribute(NodeId id, std::string_view name, std::string_view value);
    bool addBinding(NodeId id, BindingKind kind, std::string_view source, std::string_view path);
    void setPseudo(NodeId id, std::uint8_t mask) noexcept { nodes_[id.index].pseudo = mask; }

    [[nodiscard]] bool contains(NodeId id) const noexcept {
        return id.index < nodes_.size() && nodes_[id.index].alive &&
               nodes_[id.index].generation == id.generation;
    }
    [[nodiscard]] const DomNode &node(NodeId id) const noexcept { return nodes_[id.index]; }
    [[nodiscard]] DomNode &node(NodeId id) noexcept { return nodes_[id.index]; }
    [[nodiscard]] NodeId handle(std::uint32_t index) const noexcept {
        return index == noIndex ? NodeId{} : NodeId{index, nodes_[index].generation};
    }
    [[nodiscard]] NodeId root() const noexcept { return handle(root_); }
    [[nodiscard]] std::size_t size() const noexcept { return live_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return nodes_.size(); }

    [[nodiscard]] std::string_view typeName(const DomNode &node) const noexcept {
        return strings_.view(types_[node.type]);
    }
    [[nodiscard]] std::string_view text(StringId id) const noexcept { return strings_.view(id); }
    [[nodiscard]] std::optional<std::string_view> attribute(NodeId id, std::string_view name) const;
    [[nodiscard]] StringPool &strings() noexcept { return strings_; }
    [[nodiscard]] const StringPool &strings() const noexcept { return strings_; }

    void forEachChild(NodeId id, FunctionRef<void(NodeId)> visitor) const;
    [[nodiscard]] bool equivalent(const DomDocument &other) const;

  private:
    StringPool strings_;
    std::vector<StringId> types_;
    std::unordered_map<StringId, TypeId> typeLookup_;
    std::vector<DomNode> nodes_;
    std::vector<std::uint32_t> free_;
    std::uint32_t root_ = noIndex;
    std::size_t live_ = 0;

    [[nodiscard]] std::optional<TypeId> typeFor(std::string_view type);
    void unlink(std::uint32_t index);
    void link(std::uint32_t index, std::uint32_t parent);
    void release(std::uint32_t index);
    [[nodiscard]] bool equivalentNode(std::uint32_t mine, const DomDocument &other,
                                      std::uint32_t theirs) const;
};
}
