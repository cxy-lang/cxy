# Memory trace tests

Build each program with `--memory-trace`, query it over the trace socket with
`cxy utils mem`, and compare the answers with a snapshot.

Through the main runner (`run.cfg` makes it call `run.sh` for every program):

```
cd build
../tests/runner.sh -f trace/programs            # all trace tests
../tests/runner.sh -f trace/programs/threads   # one program
../tests/runner.sh -u -f trace/programs        # rewrite snapshots, review the diff
```

Standalone, without the runner:

```
tests/trace/run.sh [-f regex] [-u]      # all programs, compares by itself
tests/trace/run.sh programs/threads.cxy # prints the normalized result only
```

Options: `--cxy <path>`, `--lib <stdlib dir>`, `--trace-lib <dir with libcxy-trace.a>`
(defaults: `build/cxy`, the compiler default, `build/`). Under the runner, `--cxy`
and `--lib` are forwarded through `CXY_COMPILER` and `CXY_STDLIB`.

## Layout

- `programs/<name>.cxy`: workload. Calls `waitForHarness()` (from `_harness.cxy`)
  when the state to inspect is reached. It prints `READY`, then blocks on stdin.
- `programs/<name>.cmds`: one query per line, sent in order. `@next` releases the
  program to its next `waitForHarness()`. `@release` does the same without waiting,
  so the next query runs while the program works (a worker that starts at the
  release, for `analyze --watch`). `@mask` makes every number in the output of the
  next query `N`, for output that depends on timing. `#` starts a comment. In a query,
  `<live:N>` is replaced by the address of the Nth allocation of `list` (1 is the
  most recent), so commands that take a pointer can be tested.
- No `.cmds` file: the program runs to completion and its output (the exit leak
  report) is snapshotted.
- `programs/__snapshots/<name>.txt`: normalized output. Addresses become `PTR`, stack
  frames and repository paths are removed or replaced.

Files starting with `_` are helpers, not tests. `tests/runner.sh` skips every
`_*.cxy` file.

## Notes

- Snapshots record **today's** behavior, including that every `Class()` shares one
  allocation site. They change on purpose in phases 1, 2 and 4 of
  `docs/stage/memory-manager-plan.md`, which makes those changes visible.
- Concurrency tests assert on totals, never on order.
- Needs a `mkfifo` and `/tmp`, so it runs on macOS and Linux. It does not need
  Bash 4.
