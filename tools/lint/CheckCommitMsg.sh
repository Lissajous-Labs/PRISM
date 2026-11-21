#!/bin/sh
set -eu
if [ "${1:-}" = --self-test ]; then
    fixture=$(mktemp)
    trap 'rm -f "$fixture"' EXIT HUP INT TERM
    printf '%s\n' 'Add geometry' > "$fixture"
    "$0" "$fixture"
    for trailer in 'Co-Authored-By: Example' 'Generated-By: Example' 'Tool: Example' 'Model: Example' 'Harness: Example' 'Assisted-by: Example'; do
        printf 'Add geometry\n\n%s\n' "$trailer" > "$fixture"
        if "$0" "$fixture" 2>/dev/null; then
            exit 1
        fi
    done
    exit 0
fi
if [ "$#" -ne 1 ] || [ ! -r "$1" ]; then
    printf '%s\n' 'Expected a commit-message file' >&2
    exit 2
fi
if grep -Eiq '^[[:space:]]*(co[- ]?authored[- ]by|signed[- ]off[- ]by|authored[- ]by|generated[- ]by|assisted[- ]by|tool|model|harness)[[:space:]]*:' "$1"; then
    printf '%s\n' 'Attribution trailers are forbidden' >&2
    exit 1
fi
