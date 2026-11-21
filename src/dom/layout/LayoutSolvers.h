#pragma once

#include "dom/StyleVocabulary.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace prism::dom::layout {
inline constexpr float unbounded = std::numeric_limits<float>::infinity();

struct FlexContainer {
    float mainSize = 0;
    float crossSize = unbounded;
    float mainGap = 0;
    float crossGap = 0;
    bool column = false;
    bool reverse = false;
    bool wrap = false;
    Keyword justify = Keyword::start;
    Keyword alignItems = Keyword::stretch;
    Keyword alignContent = Keyword::stretch;
};

struct FlexItem {
    float basis = 0;
    float grow = 0;
    float shrink = 1;
    float minMain = 0;
    float maxMain = unbounded;
    std::optional<float> cross;
    float minCross = 0;
    float maxCross = unbounded;
    float marginMain = 0;
    float marginCross = 0;
    Keyword alignSelf = Keyword::automatic;
    std::int32_t order = 0;
};

struct Placement {
    float mainStart;
    float mainEnd;
    float crossStart;
    float crossEnd;
};

struct FlexResult {
    std::vector<Placement> items;
    float usedCross = 0;
    std::size_t lines = 0;
    std::size_t freezeIterations = 0;
};

void solveFlex(const FlexContainer &container, std::span<const FlexItem> items, FlexResult &result);

enum class TrackKind : std::uint8_t { fixed, percent, automatic, flex };

struct TrackBound {
    TrackKind kind = TrackKind::automatic;
    float value = 0;
};

struct Track {
    TrackBound min;
    TrackBound max;
};

[[nodiscard]] std::optional<std::vector<Track>> parseTracks(std::string_view text);

struct GridLine {
    std::int32_t start = -1;
    std::int32_t span = 1;
};

[[nodiscard]] std::optional<GridLine> parseGridLine(std::string_view text);

struct GridItem {
    GridLine column;
    GridLine row;
    float contentWidth = 0;
    float contentHeight = 0;
};

struct GridContainer {
    float width = 0;
    float height = unbounded;
    float columnGap = 0;
    float rowGap = 0;
    std::vector<Track> columns;
    std::vector<Track> rows;
};

struct GridResult {
    std::vector<float> columnEdges;
    std::vector<float> rowEdges;
    std::vector<Placement> items;
    std::vector<GridLine> resolvedColumns;
    std::vector<GridLine> resolvedRows;
};

void solveGrid(const GridContainer &container, std::span<const GridItem> items, GridResult &result);
}
