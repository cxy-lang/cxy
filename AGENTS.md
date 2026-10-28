# AI Agent Instructions for Cxy Programming Language

## CMake Project Overview

Cxy is a modern compiled programming language implemented in C. This project uses a carefully designed architecture with
arena-based memory management, comprehensive diagnostic reporting, and a multi-phase parser development approach.

## Development Philosophy

### Incremental Development

- **Feature development must be incremental and unit-testable**
- Each component should be implementable and testable in isolation
- New features require corresponding test cases before implementation
- Avoid large monolithic changes - break work into small, verifiable steps

### Building

- Build prerequisites are `cmake >= 3.16`, Clang, and LLVM (see `README.md` and required `find_package(Clang/LLVM)` in `CMakeLists.txt`).
- Configure with CMake and build with a generator-agnostic command:
    - `cmake -S . -B build`
    - `cmake --build build --parallel`
- `make -j $(nproc)` is valid in Makefile-based builds, but prefer `cmake --build` for portability (including macOS).

### Testing

- CMake/CTest is enabled by default (`ENABLE_TESTS=ON`, `ENABLE_UNIT_TESTS=ON` in `CMakeLists.txt`).
- Run unit and stdlib tests from the build directory with `ctest --output-on-failure`.
- Snapshot/lang tests use `tests/runner.sh` (see `tests/lang`, `tests/package`, and per-directory `run.cfg` behavior in
  `tests/runner.sh`).
- On macOS, `tests/runner.sh` expects Bash >= 4 and `coreutils` (`gdate`), matching `tests/README.md`.

# Output Style
- No conversational filler, pleasantries, or polite introductions.
- No step-by-step narration or explaining the obvious unless requested.
- Skip confirmations. Execute the task directly.

# Coding Style
- Write pure code using best-effort logic; do not explain your implementation choices.
- Rely strictly on your existing understanding, surrounding code blocks, or project code.
- Minimize research; if an API is unclear, provide a SHORT summary of assumptions made.
- Restrict all inline and block comments to a maximum of 2 lines.
- Leave API accuracy and build validation entirely to the user.

# Design & Research
- Limit research summaries to short, punchy bullet points.
- Maximize high-density data and minimize conversational framing.
- Focus strictly on technical facts, omitting general background or obvious context.
