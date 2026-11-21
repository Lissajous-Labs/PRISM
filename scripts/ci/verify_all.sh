#!/bin/sh
set -u
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
failed=""
skipped=""

step() {
    name=$1
    shift
    printf '== %s\n' "$name"
    if "$@"; then
        printf '== %s: pass\n' "$name"
    else
        printf '== %s: FAIL\n' "$name"
        failed="$failed $name"
    fi
}

for preset in headless sanitized release; do
    step "$preset" ./scripts/bootstrap.sh "$preset"
done

if command -v brew >/dev/null 2>&1 && [ -x "$(brew --prefix llvm 2>/dev/null)/bin/clang++" ]; then
    step realtime ./scripts/bootstrap.sh realtime
else
    skipped="$skipped realtime"
fi

if [ "$(uname)" = Darwin ]; then
    step bench sh -c 'cmake --preset bench && cmake --build --preset bench && ctest --preset bench -L bench --output-on-failure'
else
    skipped="$skipped bench"
fi

step commit-messages sh -c 'git log --format=%B | ./tools/lint/CheckCommitMsg.sh /dev/stdin'

if [ -n "${PRISM_PLUGIN_VALIDATOR:-}" ] && [ -n "${PRISM_PLUGIN_ARTIFACTS:-}" ]; then
    for artifact in $PRISM_PLUGIN_ARTIFACTS; do
        step "validate $(basename "$artifact")" \
            "$PRISM_PLUGIN_VALIDATOR" --strictness-level 10 --validate "$artifact"
    done
else
    skipped="$skipped plugin-validation"
fi

[ -n "$skipped" ] && printf 'skipped:%s\n' "$skipped"
if [ -n "$failed" ]; then
    printf 'failed:%s\n' "$failed"
    exit 1
fi
printf 'all gates green\n'
