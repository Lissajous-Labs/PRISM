#pragma once

#include "text/FontEngine.h"
#include "text/FontFile.h"
#include "text/TextShaper.h"

#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <vector>

namespace prism::bench {
struct BenchText {
    std::unique_ptr<text::FontFile> font;
    std::unique_ptr<text::TextShaper> shaper;
    std::unique_ptr<text::FontEngine> engine;
};

[[nodiscard]] inline std::optional<BenchText> loadBenchText(std::uint32_t textureBase) {
    std::ifstream file("/System/Library/Fonts/Supplemental/Arial.ttf", std::ios::binary);
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file),
                                    std::istreambuf_iterator<char>()};
    auto parsed = text::FontFile::parse(std::move(bytes));
    if (!parsed) {
        return std::nullopt;
    }
    BenchText result;
    result.font = std::make_unique<text::FontFile>(std::move(*parsed));
    const std::vector<const text::FontFile *> chain{result.font.get()};
    result.shaper = std::make_unique<text::TextShaper>(chain, 1024);
    result.engine = std::make_unique<text::FontEngine>(chain, textureBase);
    return result;
}
}
