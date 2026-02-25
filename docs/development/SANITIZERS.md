# Sanitizer Builds

Storm provides sanitizer-enabled CMake presets for catching memory, concurrency, and undefined-behavior bugs at runtime.

## Available Presets

| Preset | Sanitizers | Detects | SQLite-only variant |
|--------|-----------|---------|-------------------|
| `ninja-asan-ubsan` | ASAN + LSAN + UBSAN | Memory errors, leaks, undefined behavior | `ninja-asan-ubsan-sqlite` |
| `ninja-tsan` | TSAN | Data races in `thread_local` patterns | `ninja-tsan-sqlite` |

> **Note**: ASAN and TSAN are mutually exclusive — they cannot be combined in a single build.

## Quick Start

```bash
# Configure + build + test with ASAN+UBSAN
cmake --preset ninja-asan-ubsan && cmake --build --preset ninja-asan-ubsan
ctest --preset ninja-asan-ubsan

# SQLite only (skips PostgreSQL)
ctest --preset ninja-asan-ubsan-sqlite

# Configure + build + test with TSAN
cmake --preset ninja-tsan && cmake --build --preset ninja-tsan
ctest --preset ninja-tsan

# TSAN SQLite only
ctest --preset ninja-tsan-sqlite
```

## What Each Sanitizer Catches

### ASAN — AddressSanitizer
- Buffer overflows (stack, heap, global)
- Use-after-free
- Use-after-return / use-after-scope
- Double-free, invalid free

### LSAN — LeakSanitizer (bundled with ASAN on Linux)
On Linux, **LSAN is automatically included in ASAN** — no separate preset needed.
It runs at program exit and reports any memory that was allocated but never freed.
The `ninja-asan-ubsan` test preset sets `ASAN_OPTIONS=detect_leaks=1` to make this explicit.

### UBSAN — UndefinedBehaviorSanitizer
- Signed integer overflow
- Null pointer dereference
- Out-of-bounds array access
- Invalid enum values
- Misaligned pointer use

The `ninja-asan-ubsan` test preset sets `UBSAN_OPTIONS=print_stacktrace=1:abort_on_error=1`
so that every UB violation prints a full stack trace and aborts immediately.

### TSAN — ThreadSanitizer
- Data races between threads
- Lock-order inversions
- Races on `thread_local` storage

Particularly relevant for Storm's `thread_local` connection pattern.
The test preset uses `jobs: 1` (serial execution) for clean, non-interleaved output.

## Build Directories

Each sanitizer preset uses its own binary directory to avoid cache conflicts:

| Preset | Binary dir |
|--------|-----------|
| `ninja-debug` | `build/debug` |
| `ninja-asan-ubsan` | `build/asan-ubsan` |
| `ninja-tsan` | `build/tsan` |

## Important Notes

- **No coverage**: Sanitizer presets do not enable `ENABLE_COVERAGE`. Coverage and sanitizers conflict — use `ninja-debug` for coverage.
- **Serial tests**: Sanitizer test presets run with `jobs: 1` for readable output. Expect ~35 seconds per full run.
- **Stack traces**: All presets add `-fno-omit-frame-pointer` for accurate stack traces in sanitizer reports.
- **False positives**: TSAN may produce false positives if SQLite or libpq were not compiled with TSAN. If you see races only in third-party code, suppress them with a TSAN suppression file.

## MSAN — MemorySanitizer (not yet available)

MSAN detects reads from **uninitialized memory** — a bug class not covered by the other sanitizers.

It is tracked in issue [#116](https://github.com/spiritEcosse/storm/issues/116) but is not currently available because MSAN requires **every library in the dependency chain** (including libc++) to be compiled with MSAN instrumentation. Storm depends on a custom `clang-p2996` build for C++26 reflection support, and rebuilding this entire toolchain with MSAN is not practical until C++26 / P2996 reflection stabilizes in upstream Clang.
