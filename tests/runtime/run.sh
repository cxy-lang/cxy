#!/usr/bin/env bash
# Builds and runs rc_tests.c once per variant listed in variants.cfg.
#   run.sh               every variant marked `on` (ctest runs this)
#   run.sh -v asan       only the named variant, even when it is `off`
# Environment: CC (default cc), RC_TEST_TIMEOUT seconds per run (default 120).

HERE=$(realpath "$(dirname "$0")")
ROOT=$(realpath "$HERE/../..")
CC="${CC:-cc}"
TIMEOUT="${RC_TEST_TIMEOUT:-120}"
ONLY=""
SRC="$HERE/rc_tests.c"

while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--variant) ONLY="$2"; shift 2 ;;
        -h|--help) sed -n '2,5p' "$0"; exit 0 ;;
        *) echo "unknown option $1"; exit 1 ;;
    esac
done

work=$(mktemp -d /tmp/cxy-rc.XXXXXX)
trap 'rm -rf "$work"' EXIT

run_variant() { # <name> <flags...>
    local name=$1
    shift
    local bin="$work/$name"
    echo "Running runtime tests [$name]"
    # shellcheck disable=SC2068
    if ! "$CC" -std=gnu11 -Wall -Wno-unused-function -DCXY_RUNTIME_TEST $@ \
        -I"$ROOT/src/cxy/runtime" -o "$bin" "$SRC" -lpthread; then
        echo "  FAILED: build of [$name]"
        return 1
    fi

    "$bin" &
    local pid=$! i
    for ((i = 0; i < TIMEOUT * 10; i++)); do
        kill -0 "$pid" 2> /dev/null || break
        sleep 0.1
    done
    if kill -0 "$pid" 2> /dev/null; then
        kill -9 "$pid" 2> /dev/null
        wait "$pid" 2> /dev/null
        echo "  FAILED: [$name] timed out after ${TIMEOUT}s"
        return 1
    fi
    wait "$pid" || {
        echo "  FAILED: [$name]"
        return 1
    }
}

passed=0
failed=0
while read -r name enabled flags; do
    case "$name" in ''|';'*) continue ;; esac
    if [ -n "$ONLY" ]; then
        [ "$name" == "$ONLY" ] || continue
    elif [ "$enabled" != "on" ]; then
        continue
    fi
    # shellcheck disable=SC2086
    if run_variant "$name" $flags; then
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
done < "$HERE/variants.cfg"

if [ -n "$ONLY" ] && [ $((passed + failed)) -eq 0 ]; then
    echo "no variant named '$ONLY' in variants.cfg"
    exit 1
fi

echo "runtime tests: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
