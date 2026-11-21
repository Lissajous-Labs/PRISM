#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
if [ -d .git ]; then
    git config core.hooksPath .githooks
fi
preset=${1:-headless}
cmake --preset "$preset"
cmake --build --preset "$preset"
ctest --preset "$preset"
