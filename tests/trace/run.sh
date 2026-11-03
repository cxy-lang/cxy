#!/usr/bin/env bash
# Memory trace harness. Builds each programs/<name>.cxy with --memory-trace and
# either queries it over the trace socket (programs/<name>.cmds) or runs it to
# completion and snapshots its output (the exit-time leak report).
#
# `run.sh <programs/name.cxy>` prints the normalized result of one program and
# leaves the comparison to the caller. tests/runner.sh does that through
# tests/trace/run.cfg, with snapshots in programs/__snapshots/. Without a file
# argument the script runs every program and compares by itself.
#
# Command file lines: a trace query (`summary`, `sites`, `mark`, ...), `@next`
# (release the program to its next waitForHarness() call), `#` comments.

HERE=$(realpath "$(dirname "$0")")
ROOT=$(realpath "$HERE/../..")
CXY="${CXY_COMPILER:-${CXY:-$ROOT/build/cxy}}"
STDLIB="${CXY_STDLIB:-}"
SINGLE=""
TRACE_LIB="${CXY_MEMORY_TRACE_LIB_DIR:-$ROOT/build}"
UPDATE=""
FILTER=""
WAIT_MS=30000

# The program may be gone when the last release byte is sent
trap '' PIPE

usage() {
    cat << EOF
Usage: $(basename "$0") [OPTIONS] [programs/name.cxy]
  -f, --filter <regex>   only programs whose name matches
  -u, --update           rewrite snapshots with the current output
      --cxy <path>       compiler binary (default: build/cxy, or \$CXY)
      --lib <path>       stdlib directory (passed as --stdlib)
      --trace-lib <dir>  directory with libcxy-trace.a (default: build, or
                         \$CXY_MEMORY_TRACE_LIB_DIR)
  -h, --help
EOF
}

while [[ $# -gt 0 ]]; do
    case $1 in
        -f|--filter) FILTER="$2"; shift 2 ;;
        -u|--update) UPDATE=1; shift ;;
        --cxy) CXY="$2"; shift 2 ;;
        --lib) STDLIB="$2"; shift 2 ;;
        --trace-lib) TRACE_LIB="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *.cxy) SINGLE="$1"; shift ;;
        *) echo "unknown option $1"; usage; exit 1 ;;
    esac
done

# Addresses, absolute paths and stack frames are not stable between runs
normalize() {
    sed -E \
        -e "s#${ROOT}#<ROOT>#g" \
        -e 's#/tmp/cxy-trace\.[A-Za-z0-9]+#<TMP>#g' \
        -e '/^ +#[0-9]+ /d' \
        -e '/Allocation callstack:/d' \
        -e 's/0x[0-9a-fA-F]+/PTR/g'
}

# <description> <sh command>: polls the command with `cxy utils wait-for`
wait_for() {
    "$CXY" utils wait-for --timeout "$WAIT_MS" --period 100 "$2" > /dev/null 2>&1 && return 0
    echo "timed out waiting for $1" >&2
    return 1
}

# <work dir> <n>: shell test, the program printed READY at least n times
ready_at_least() { echo "[ \"\$(grep -c '^READY\$' '$1/out.txt')\" -ge $2 ]"; }

run_program() {
    local name=$1
    local src="$HERE/programs/$name.cxy"
    local cmds="$HERE/programs/$name.cmds"
    local snap="$HERE/programs/__snapshots/$name.txt"
    local work
    work=$(mktemp -d /tmp/cxy-trace.XXXXXX)
    local bin="$work/prog" sock="$work/mem.sock" pid="" status=0

    cleanup() {
        [ -n "$pid" ] && kill "$pid" 2> /dev/null
        exec 3>&-
        rm -rf "$work"
    }

    local stdlib_args=()
    [ -n "$STDLIB" ] && stdlib_args=(--stdlib "$STDLIB")
    local build_out
    build_out=$(CXY_MEMORY_TRACE_LIB_DIR="$TRACE_LIB" "$CXY" dev --memory-trace \
        --no-color --no-progress --dump-stats NONE "${stdlib_args[@]}" \
        -o "$bin" "$src" 2>&1)
    if [ $? -ne 0 ]; then
        echo "  FAILED: build of $name"
        echo "$build_out"
        cleanup
        return 1
    fi

    : > "$work/result.txt"
    : > "$work/out.txt"
    if [ -f "$cmds" ]; then
        mkfifo "$work/in"
        CXY_MEM_SOCKET="$sock" "$bin" < "$work/in" > "$work/out.txt" 2>&1 &
        pid=$!
        exec 3> "$work/in"

        local phase=1
        if ! wait_for "READY" "$(ready_at_least "$work" 1)" ||
            ! wait_for "query socket" "test -S '$sock'"; then
            cat "$work/out.txt"
            cleanup
            return 1
        fi

        local line
        while IFS= read -r line || [ -n "$line" ]; do
            case "$line" in
                ''|'#'*) continue ;;
                '@next')
                    echo "@next" >> "$work/result.txt"
                    phase=$((phase + 1))
                    printf x >&3
                    if ! wait_for "READY #$phase" "$(ready_at_least "$work" "$phase")"; then
                        cleanup
                        return 1
                    fi
                    ;;
                *)
                    echo "\$ $line" >> "$work/result.txt"
                    # shellcheck disable=SC2086
                    "$CXY" utils mem --socket "$sock" $line nosym >> "$work/result.txt" 2>&1
                    ;;
            esac
        done < "$cmds"

        printf x >&3
        exec 3>&-
        wait_for "program exit" "! kill -0 $pid"
        wait "$pid" 2> /dev/null
        pid=""
        {
            echo "\$ program output"
            grep -v -e '^READY$' -e '^\[cxy-mem\] query socket' "$work/out.txt"
        } >> "$work/result.txt"
    else
        "$bin" < /dev/null > "$work/out.txt" 2>&1
        {
            echo "\$ program output"
            grep -v '^\[cxy-mem\] query socket' "$work/out.txt"
        } >> "$work/result.txt"
    fi

    local actual
    actual=$(normalize < "$work/result.txt")
    if [ -n "$SINGLE" ]; then
        echo "$actual"
    elif [ -n "$UPDATE" ]; then
        mkdir -p "$(dirname "$snap")"
        echo "$actual" > "$snap"
        echo "  updated $name"
    elif [ ! -f "$snap" ]; then
        echo "  FAILED: no snapshot for $name (run with -u)"
        status=1
    elif ! diff -u --label "Expected: $snap" --label "Actual: $name" "$snap" <(echo "$actual"); then
        echo "  FAILED: output of $name does not match the snapshot"
        status=1
    fi

    cleanup
    return $status
}

if [ ! -x "$CXY" ]; then
    echo "compiler not found at $CXY (use --cxy)"
    exit 1
fi

if [ -n "$SINGLE" ]; then
    run_program "$(basename "$SINGLE" .cxy)"
    exit $?
fi

PASSED=0
FAILED=0
for src in "$HERE"/programs/*.cxy; do
    name=$(basename "$src" .cxy)
    case "$name" in _*) continue ;; esac
    if [ -n "$FILTER" ] && ! echo "$name" | grep -Eq "$FILTER"; then
        continue
    fi
    echo "Running trace test $name"
    if run_program "$name"; then
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
    fi
done

echo "trace tests: $PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
