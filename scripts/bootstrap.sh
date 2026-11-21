#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
preset=${1:-headless}
cmake --preset "$preset"
cmake --build --preset "$preset"
ctest --preset "$preset"
