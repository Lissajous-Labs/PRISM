#pragma once

#include "dom/PerfectHash.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace prism::dom {
enum class Property : std::uint16_t {
    color,
    background,
    borderColor,
    opacity,
    borderRadius,
    translateX,
    translateY,
    scale,
    rotate,
    width,
    height,
    minWidth,
    minHeight,
    maxWidth,
    maxHeight,
    margin,
    padding,
    gap,
    flexBasis,
    left,
    top,
    right,
    bottom,
    borderWidth,
    flexGrow,
    flexShrink,
    fontSize,
    lineHeight,
    order,
    display,
    flexDirection,
    flexWrap,
    justifyContent,
    alignItems,
    alignSelf,
    visibility,
    transition,
    count
};

inline constexpr std::size_t propertyCount = static_cast<std::size_t>(Property::count);

inline constexpr std::array<std::string_view, propertyCount> propertyNames{
    "color",          "background",    "border-color",
    "opacity",        "border-radius", "translate-x",
    "translate-y",    "scale",         "rotate",
    "width",          "height",        "min-width",
    "min-height",     "max-width",     "max-height",
    "margin",         "padding",       "gap",
    "flex-basis",     "left",          "top",
    "right",          "bottom",        "border-width",
    "flex-grow",      "flex-shrink",   "font-size",
    "line-height",    "order",         "display",
    "flex-direction", "flex-wrap",     "justify-content",
    "align-items",    "align-self",    "visibility",
    "transition"};

enum class Keyword : std::uint16_t {
    none,
    flex,
    grid,
    anchored,
    tabbed,
    patchCanvas,
    row,
    column,
    rowReverse,
    columnReverse,
    nowrap,
    wrap,
    start,
    end,
    center,
    stretch,
    baseline,
    spaceBetween,
    spaceAround,
    spaceEvenly,
    visible,
    hidden,
    automatic,
    transparent,
    count
};

inline constexpr std::array<std::string_view, static_cast<std::size_t>(Keyword::count)>
    keywordNames{"none",         "flex",     "grid",          "anchored",     "tabbed",
                 "patch-canvas", "row",      "column",        "row-reverse",  "column-reverse",
                 "nowrap",       "wrap",     "start",         "end",          "center",
                 "stretch",      "baseline", "space-between", "space-around", "space-evenly",
                 "visible",      "hidden",   "auto",          "transparent"};

inline constexpr std::array<std::string_view, 7> pseudoClassNames{
    "hover", "active", "focus", "disabled", "checked", "modulated", "selected"};

enum class Unit : std::uint8_t { none, px, percent, em, vw, vh, vmin, automatic };

inline constexpr std::array<std::string_view, 6> unitNames{"px", "%", "em", "vw", "vh", "vmin"};

inline constexpr auto propertyTable = buildPerfectHash(propertyNames);
inline constexpr auto keywordTable = buildPerfectHash(keywordNames);
inline constexpr auto pseudoTable = buildPerfectHash(pseudoClassNames);
inline constexpr auto unitTable = buildPerfectHash(unitNames);

[[nodiscard]] constexpr bool inherited(Property property) noexcept {
    return property == Property::color || property == Property::fontSize ||
           property == Property::lineHeight || property == Property::visibility;
}
}
