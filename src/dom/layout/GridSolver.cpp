#include "dom/layout/LayoutSolvers.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace prism::dom::layout {
namespace {
std::string_view trim(std::string_view text) {
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<float> number(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    float value = 0;
    float scale = 0;
    for (const char c : text) {
        if (c == '.' && scale == 0) {
            scale = 1;
        } else if (c >= '0' && c <= '9') {
            value = value * 10 + static_cast<float>(c - '0');
            scale *= 10;
        } else {
            return std::nullopt;
        }
    }
    return scale > 0 ? value / scale : value;
}

std::optional<TrackBound> bound(std::string_view text) {
    text = trim(text);
    if (text == "auto") {
        return TrackBound{TrackKind::automatic, 0};
    }
    const std::pair<std::string_view, TrackKind> suffixes[] = {
        {"px", TrackKind::fixed}, {"%", TrackKind::percent}, {"fr", TrackKind::flex}};
    for (const auto &[suffix, kind] : suffixes) {
        if (text.size() > suffix.size() && text.substr(text.size() - suffix.size()) == suffix) {
            const auto value = number(text.substr(0, text.size() - suffix.size()));
            if (!value) {
                return std::nullopt;
            }
            return TrackBound{kind, *value};
        }
    }
    if (text == "0") {
        return TrackBound{TrackKind::fixed, 0};
    }
    return std::nullopt;
}

std::vector<std::string_view> splitTopLevel(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    int depth = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const char c = i < text.size() ? text[i] : separator;
        depth += c == '(' ? 1 : (c == ')' ? -1 : 0);
        if (depth == 0 && c == separator) {
            const auto part = trim(text.substr(start, i - start));
            if (!part.empty()) {
                parts.push_back(part);
            }
            start = i + 1;
        }
    }
    return parts;
}

bool appendTracks(std::string_view text, std::vector<Track> &out, int depth) {
    for (const std::string_view token : splitTopLevel(text, ' ')) {
        if (token.substr(0, 7) == "repeat(" && token.back() == ')' && depth == 0) {
            const auto args = splitTopLevel(token.substr(7, token.size() - 8), ',');
            const auto times = args.size() == 2 ? number(args[0]) : std::nullopt;
            if (!times || *times < 1 || *times > 1000) {
                return false;
            }
            for (int i = 0; i < static_cast<int>(*times); ++i) {
                if (!appendTracks(args[1], out, depth + 1)) {
                    return false;
                }
            }
        } else if (token.substr(0, 7) == "minmax(" && token.back() == ')') {
            const auto args = splitTopLevel(token.substr(7, token.size() - 8), ',');
            const auto low = args.size() == 2 ? bound(args[0]) : std::nullopt;
            const auto high = args.size() == 2 ? bound(args[1]) : std::nullopt;
            if (!low || !high || low->kind == TrackKind::flex) {
                return false;
            }
            out.push_back({*low, *high});
        } else {
            const auto single = bound(token);
            if (!single) {
                return false;
            }
            out.push_back({single->kind == TrackKind::flex ? TrackBound{} : *single, *single});
        }
    }
    return true;
}

struct Sizing {
    float base;
    float limit;
    float flex;
};

void sizeTracks(const std::vector<Track> &tracks, float available, float gap,
                std::span<const GridLine> spans, std::span<const float> content,
                std::vector<float> &sizes) {
    const std::size_t n = tracks.size();
    const bool definite = std::isfinite(available);
    std::vector<Sizing> sizing(n);
    const auto resolve = [&](const TrackBound &b) {
        switch (b.kind) {
        case TrackKind::fixed:
            return b.value;
        case TrackKind::percent:
            return definite ? available * b.value / 100 : 0.0f;
        default:
            return 0.0f;
        }
    };
    for (std::size_t i = 0; i < n; ++i) {
        const Track &t = tracks[i];
        const float base = resolve(t.min);
        float limit = unbounded;
        if (t.max.kind == TrackKind::fixed || (t.max.kind == TrackKind::percent && definite)) {
            limit = std::max(base, resolve(t.max));
        }
        sizing[i] = {base, limit, t.max.kind == TrackKind::flex ? t.max.value : 0};
    }
    std::int32_t largestSpan = 1;
    for (const GridLine &line : spans) {
        largestSpan = std::max(largestSpan, line.span);
    }
    for (std::int32_t span = 1; span <= largestSpan; ++span) {
        for (std::size_t k = 0; k < spans.size(); ++k) {
            if (spans[k].span != span) {
                continue;
            }
            const auto first = static_cast<std::size_t>(spans[k].start);
            bool spansFlex = false;
            float covered = gap * static_cast<float>(span - 1);
            std::vector<std::size_t> growable;
            for (std::size_t i = first; i < first + static_cast<std::size_t>(span); ++i) {
                spansFlex = spansFlex || sizing[i].flex > 0;
                covered += sizing[i].base;
                if (tracks[i].min.kind == TrackKind::automatic) {
                    growable.push_back(i);
                }
            }
            if ((spansFlex && span > 1) || content[k] <= covered || growable.empty()) {
                continue;
            }
            const float share = (content[k] - covered) / static_cast<float>(growable.size());
            for (const std::size_t i : growable) {
                sizing[i].base += share;
                sizing[i].limit = std::max(sizing[i].limit, sizing[i].base);
            }
        }
    }
    const float gaps = gap * static_cast<float>(n > 0 ? n - 1 : 0);
    const auto total = [&] {
        float sum = gaps;
        for (const Sizing &s : sizing) {
            sum += s.base;
        }
        return sum;
    };
    if (definite) {
        for (int pass = 0; pass < 8; ++pass) {
            float free = available - total();
            std::size_t open = 0;
            for (const Sizing &s : sizing) {
                open += s.flex == 0 && std::isfinite(s.limit) && s.limit > s.base ? 1 : 0;
            }
            if (free <= 0 || open == 0) {
                break;
            }
            const float share = free / static_cast<float>(open);
            for (Sizing &s : sizing) {
                if (s.flex == 0 && std::isfinite(s.limit) && s.limit > s.base) {
                    s.base = std::min(s.limit, s.base + share);
                }
            }
        }
    }
    bool anyFlex = false;
    for (const Sizing &s : sizing) {
        anyFlex = anyFlex || s.flex > 0;
    }
    if (anyFlex) {
        std::vector<bool> inflexible(n, false);
        float fraction = 0;
        if (definite) {
            for (bool changed = true; changed;) {
                changed = false;
                float leftover = available - gaps;
                float factors = 0;
                for (std::size_t i = 0; i < n; ++i) {
                    if (sizing[i].flex > 0 && !inflexible[i]) {
                        factors += sizing[i].flex;
                    } else {
                        leftover -= sizing[i].base;
                    }
                }
                fraction = std::max(0.0f, leftover) / std::max(1.0f, factors);
                for (std::size_t i = 0; i < n; ++i) {
                    if (sizing[i].flex > 0 && !inflexible[i] &&
                        sizing[i].base > fraction * sizing[i].flex) {
                        inflexible[i] = true;
                        changed = true;
                    }
                }
            }
        } else {
            for (const Sizing &s : sizing) {
                if (s.flex > 0) {
                    fraction = std::max(fraction, s.base / std::max(1.0f, s.flex));
                }
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (sizing[i].flex > 0) {
                sizing[i].base = std::max(sizing[i].base, fraction * sizing[i].flex);
            }
        }
    } else if (definite) {
        std::size_t stretchable = 0;
        for (std::size_t i = 0; i < n; ++i) {
            stretchable += tracks[i].max.kind == TrackKind::automatic ? 1 : 0;
        }
        const float free = available - total();
        if (free > 0 && stretchable > 0) {
            for (std::size_t i = 0; i < n; ++i) {
                if (tracks[i].max.kind == TrackKind::automatic) {
                    sizing[i].base += free / static_cast<float>(stretchable);
                }
            }
        }
    }
    sizes.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        sizes[i] = sizing[i].base;
    }
}

void edgesFrom(std::span<const float> sizes, float gap, std::vector<float> &edges) {
    edges.resize(sizes.size() * 2);
    float cursor = 0;
    for (std::size_t i = 0; i < sizes.size(); ++i) {
        edges[2 * i] = cursor;
        edges[2 * i + 1] = cursor + sizes[i];
        cursor = edges[2 * i + 1] + gap;
    }
}
}

std::optional<std::vector<Track>> parseTracks(std::string_view text) {
    std::vector<Track> tracks;
    if (!appendTracks(text, tracks, 0)) {
        return std::nullopt;
    }
    return tracks;
}

std::optional<GridLine> parseGridLine(std::string_view text) {
    text = trim(text);
    const auto parseInt = [](std::string_view digits) -> std::optional<std::int32_t> {
        const auto value = number(trim(digits));
        if (!value || *value < 1 || *value > 10000 || *value != std::floor(*value)) {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(*value);
    };
    const auto slash = text.find('/');
    const std::string_view head = trim(text.substr(0, slash));
    GridLine line;
    if (head.substr(0, 5) == "span ") {
        const auto span = parseInt(head.substr(5));
        if (!span || slash != std::string_view::npos) {
            return std::nullopt;
        }
        line.span = *span;
        return line;
    }
    const auto start = parseInt(head);
    if (!start) {
        return std::nullopt;
    }
    line.start = *start - 1;
    if (slash == std::string_view::npos) {
        return line;
    }
    const std::string_view tail = trim(text.substr(slash + 1));
    if (tail.substr(0, 5) == "span ") {
        const auto span = parseInt(tail.substr(5));
        if (!span) {
            return std::nullopt;
        }
        line.span = *span;
        return line;
    }
    const auto end = parseInt(tail);
    if (!end || *end <= *start) {
        return std::nullopt;
    }
    line.span = *end - *start;
    return line;
}

void solveGrid(const GridContainer &container, std::span<const GridItem> items,
               GridResult &result) {
    std::vector<Track> columns =
        container.columns.empty() ? std::vector<Track>{Track{}} : container.columns;
    std::vector<Track> rows = container.rows;
    result.resolvedColumns.assign(items.size(), {});
    result.resolvedRows.assign(items.size(), {});
    std::vector<std::vector<bool>> occupied;
    const auto ensure = [&](std::size_t rowCount, std::size_t columnCount) {
        while (columns.size() < columnCount) {
            columns.push_back(Track{});
        }
        while (rows.size() < rowCount) {
            rows.push_back(Track{});
        }
        while (occupied.size() < rowCount) {
            occupied.emplace_back();
        }
        for (auto &row : occupied) {
            row.resize(std::max(row.size(), columns.size()), false);
        }
    };
    const auto fits = [&](std::size_t row, std::size_t column, const GridItem &item) {
        ensure(row + static_cast<std::size_t>(item.row.span),
               column + static_cast<std::size_t>(item.column.span));
        for (std::size_t r = row; r < row + static_cast<std::size_t>(item.row.span); ++r) {
            for (std::size_t c = column; c < column + static_cast<std::size_t>(item.column.span);
                 ++c) {
                if (occupied[r][c]) {
                    return false;
                }
            }
        }
        return true;
    };
    const auto place = [&](std::size_t k, std::size_t row, std::size_t column) {
        const GridItem &item = items[k];
        for (std::size_t r = row; r < row + static_cast<std::size_t>(item.row.span); ++r) {
            for (std::size_t c = column; c < column + static_cast<std::size_t>(item.column.span);
                 ++c) {
                occupied[r][c] = true;
            }
        }
        result.resolvedColumns[k] = {static_cast<std::int32_t>(column), item.column.span};
        result.resolvedRows[k] = {static_cast<std::int32_t>(row), item.row.span};
    };
    for (int phase = 0; phase < 3; ++phase) {
        std::size_t cursorRow = 0;
        std::size_t cursorColumn = 0;
        for (std::size_t k = 0; k < items.size(); ++k) {
            const GridItem &item = items[k];
            const bool fixedColumn = item.column.start >= 0;
            const bool fixedRow = item.row.start >= 0;
            const int itemPhase = fixedColumn && fixedRow ? 0 : (fixedRow ? 1 : 2);
            if (itemPhase != phase) {
                continue;
            }
            if (fixedColumn && fixedRow) {
                ensure(static_cast<std::size_t>(item.row.start + item.row.span),
                       static_cast<std::size_t>(item.column.start + item.column.span));
                place(k, static_cast<std::size_t>(item.row.start),
                      static_cast<std::size_t>(item.column.start));
            } else if (fixedRow) {
                std::size_t column = 0;
                while (!fits(static_cast<std::size_t>(item.row.start), column, item)) {
                    ++column;
                }
                place(k, static_cast<std::size_t>(item.row.start), column);
            } else if (fixedColumn) {
                std::size_t row = 0;
                while (!fits(row, static_cast<std::size_t>(item.column.start), item)) {
                    ++row;
                }
                place(k, row, static_cast<std::size_t>(item.column.start));
            } else {
                const std::size_t width =
                    std::max(columns.size(), static_cast<std::size_t>(item.column.span));
                for (;;) {
                    if (cursorColumn + static_cast<std::size_t>(item.column.span) > width) {
                        cursorColumn = 0;
                        ++cursorRow;
                        continue;
                    }
                    if (fits(cursorRow, cursorColumn, item)) {
                        break;
                    }
                    ++cursorColumn;
                }
                place(k, cursorRow, cursorColumn);
                cursorColumn += static_cast<std::size_t>(item.column.span);
            }
        }
    }
    ensure(rows.size(), columns.size());
    std::vector<float> contentWidths(items.size());
    std::vector<float> contentHeights(items.size());
    for (std::size_t k = 0; k < items.size(); ++k) {
        contentWidths[k] = items[k].contentWidth;
        contentHeights[k] = items[k].contentHeight;
    }
    std::vector<float> columnSizes;
    std::vector<float> rowSizes;
    sizeTracks(columns, container.width, container.columnGap, result.resolvedColumns, contentWidths,
               columnSizes);
    sizeTracks(rows, container.height, container.rowGap, result.resolvedRows, contentHeights,
               rowSizes);
    edgesFrom(columnSizes, container.columnGap, result.columnEdges);
    edgesFrom(rowSizes, container.rowGap, result.rowEdges);
    result.items.resize(items.size());
    for (std::size_t k = 0; k < items.size(); ++k) {
        const GridLine &column = result.resolvedColumns[k];
        const GridLine &row = result.resolvedRows[k];
        result.items[k] = {
            result.columnEdges[2 * static_cast<std::size_t>(column.start)],
            result.columnEdges[2 * static_cast<std::size_t>(column.start + column.span) - 1],
            result.rowEdges[2 * static_cast<std::size_t>(row.start)],
            result.rowEdges[2 * static_cast<std::size_t>(row.start + row.span) - 1]};
    }
}
}
