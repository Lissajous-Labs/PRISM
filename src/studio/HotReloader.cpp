#include "studio/HotReloader.h"

#include "math/Hash64.h"

#include <fstream>
#include <iterator>
#include <optional>

namespace prism::studio {
namespace {
std::optional<std::string> readText(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
}

void HotReloader::watch(std::filesystem::path path, WatchKind kind) {
    Entry entry{std::move(path), kind};
    if (const auto text = readText(entry.path)) {
        entry.hash = math::hash64(std::string_view(*text));
        entry.seen = true;
    }
    entries_.push_back(std::move(entry));
}

ReloadReport HotReloader::poll() {
    ReloadReport report;
    for (auto &entry : entries_) {
        const auto text = readText(entry.path);
        if (!text) {
            continue;
        }
        const auto hash = math::hash64(std::string_view(*text));
        if (entry.seen && hash == entry.hash) {
            continue;
        }
        entry.hash = hash;
        entry.seen = true;
        ++report.changed;
        const bool ok = entry.kind == WatchKind::document ? session_->reloadDocument(*text)
                                                          : session_->reloadStyles(*text);
        ++(ok ? report.applied : report.rejected);
    }
    return report;
}
}
