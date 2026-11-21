#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
if [ -d .git ]; then
    git config core.hooksPath .githooks
fi
preset=${1:-headless}
if [ "$preset" = realtime ] && [ -z "${CXX:-}" ] && command -v brew >/dev/null 2>&1; then
    llvm=$(brew --prefix llvm)
    export CC="$llvm/bin/clang" CXX="$llvm/bin/clang++" OBJCXX="$llvm/bin/clang++"
fi
cmake --preset "$preset"
cmake --build --preset "$preset"
ctest --preset "$preset"
