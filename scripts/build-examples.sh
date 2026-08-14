#!/usr/bin/env bash
#
# Compile every ported example for both supported boards.
#
# Each sketch must build for BOTH boards even when it only functions on one --
# the unsupported board falls through to a stub setup()/loop(). A sketch that
# fails to compile for the other board is a defect in that stub.
#
# Usage:
#   ./scripts/build-examples.sh                  # all examples, both boards
#   ./scripts/build-examples.sh twatch_ultra     # all examples, one board
#   ./scripts/build-examples.sh "" ui/SimpleWatch  # one example, both boards
#
set -uo pipefail

cd "$(dirname "$0")/.."

BOARDS=${1:-"twatchs3 twatch_ultra"}
FILTER=${2:-""}

# Discover sketches rather than hardcoding the list, so new ones are picked up.
mapfile -t SKETCHES < <(find examples -name '*.ino' -printf '%h\n' | sort)

if [ ${#SKETCHES[@]} -eq 0 ]; then
    echo "No sketches found under examples/" >&2
    exit 1
fi

pass=0
fail=0
failed_list=()

for sketch in "${SKETCHES[@]}"; do
    rel=${sketch#examples/}

    if [ -n "$FILTER" ] && [[ "$rel" != *"$FILTER"* ]]; then
        continue
    fi

    for board in $BOARDS; do
        printf '%-46s %-14s ' "$rel" "$board"

        if PLATFORMIO_SRC_DIR="$sketch" pio run -e "$board" >/tmp/build-example.log 2>&1; then
            echo "OK"
            pass=$((pass + 1))
        else
            echo "FAILED"
            fail=$((fail + 1))
            failed_list+=("$rel [$board]")
            # Surface the actual compiler errors, not just the failure.
            grep -E "error:|fatal error:" /tmp/build-example.log | head -5 | sed 's/^/    /'
        fi
    done
done

echo
echo "passed: $pass   failed: $fail"

if [ $fail -gt 0 ]; then
    echo
    echo "Failures:"
    printf '  %s\n' "${failed_list[@]}"
    exit 1
fi
