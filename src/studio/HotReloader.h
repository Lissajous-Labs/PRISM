#pragma once

#include "studio/StudioSession.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace prism::studio {
enum class WatchKind : std::uint8_t { document, styles };

struct ReloadReport {
    std::size_t changed = 0;
    std::size_t applied = 0;
    std::size_t rejected = 0;
};

class HotReloader {
  public:
    explicit HotReloader(StudioSession &session) : session_(&session) {}

    void watch(std::filesystem::path path, WatchKind kind);
    ReloadReport poll();

  private:
    struct Entry {
        std::filesystem::path path;
        WatchKind kind;
        std::uint64_t hash = 0;
        bool seen = false;
    };
    StudioSession *session_;
    std::vector<Entry> entries_;
};
}
