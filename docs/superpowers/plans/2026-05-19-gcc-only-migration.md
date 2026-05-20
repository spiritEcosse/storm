# GCC-only Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the experimental `clang-p2996` fork + custom libc++ build with stock GCC 16 + libstdc++ as Storm's sole compiler, getting the full test suite green and CI rebuilt around it.

**Architecture:** GCC 16.1.1 ships P2996 reflection (`-freflection`) that covers Storm's complete reflection surface (verified 2026-05-19). The migration replaces `import <header>;` header units with `import std;` (CMake `CXX_MODULE_STD ON` opt-in), introduces a single `storm_c_headers` module to isolate C-system headers from `import std;` (the one technical wall this project hits), and removes all clang-p2996 / libc++ infrastructure. Source-level fallout (bare `size_t` etc.) is fixed by `using std::…;` injections.

**Tech Stack:** GCC 16+, libstdc++, CMake 3.30+, Ninja, `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD = 451f2fe2-a8a2-47c3-bc32-94786d8fc91b`.

---

## File Structure

**New files (1):**
- `src/storm_c_headers.cppm` — Single module unit that `#include`s all C-system headers (`<sqlite3.h>`, `<libpq-fe.h>`, `<uuid.h>`, `<plf_hive/plf_hive.h>`) in its global-module fragment and re-exports the needed types/functions via `using ::name;` declarations. Consumers `import storm_c_headers;` instead of `#include`-ing those headers, avoiding the `__mbstate_t` redeclaration conflict with `import std;`.

**Files deleted (5):**
- `cmake/libcxx.cmake` — libc++/LIBCXX_ROOT wiring is gone.
- `cmake/format.cmake` — references hard-coded `../clang-p2996/build/bin/clang-format`; will be rewritten in Phase 6 as `cmake/format.cmake` (system clang-format) or merged into another module.
- `.github/workflows/docker-ci-image.yml` — no more `storm-ci` image (depends on clang-p2996).
- `scripts/run_clang_tidy.sh` — replaced in Phase 6 with a stub or removed.
- `.claude/agents/clang.md` — clang-specific agent file; obsolete.

**Files modified (heavy edits in bold):**
- **`CMakeLists.txt`** — compiler-gate, global flags, drop `include(cmake/libcxx.cmake)`, add `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD` UUID.
- **`CMakePresets.json`** — drop clang `base` preset, repoint `ninja-debug`/`ninja-release` at GCC.
- **`cmake/coverage.cmake`** — switch from `llvm-profdata`/`llvm-cov` (via `LIBCXX_ROOT/build/bin/`) to system `gcov`/`lcov`.
- **`cmake/sanitizers.cmake`** — verify GCC sanitizer flags; no per-flag changes expected but the underlying `cmake-scripts` may need GCC-aware overrides.
- `tests/CMakeLists.txt` — swap `-fconstexpr-steps=4194304` for `-fconstexpr-ops-limit=134217728`, drop the now-unused generator expression.
- `benchmarks/CMakeLists.txt`, `fuzz/CMakeLists.txt`, `cmake/storm_migrations.cmake`, `benchmarks/dashboard/CMakeLists.txt`, `tests/mock_sqlite/CMakeLists.txt`, `tests/mock_libpq/CMakeLists.txt` — adjust `apply_cxx_flags` definition site only; call sites stay.
- **24 `.cppm` source files** under `src/` — replace `import <foo>;` lines with `import std;` and add `using std::size_t;` (and related) blocks. Files inventoried in Phase 2.
- **45 `.cpp`/`.hpp` test/bench files** — same header-unit swap. Inventoried in Phase 2.
- **`src/storm.cppm`, `src/db/postgresql_connection.cppm`, `src/db/postgresql_statement.cppm`, `src/db/sqlite.cppm`, `src/orm/queryset.cppm`, `src/orm/statements/aggregate.cppm`, `src/orm/statements/distinct.cppm`, `src/orm/statements/select.cppm`, `src/orm/statements/setop.cppm`, `src/orm/utilities.cppm`** — drop their direct `#include` of C headers, add `import storm_c_headers;` instead.
- **`.github/workflows/ci.yml`** — drop `storm-ci` image, install GCC 16 directly, retarget preset names.
- **`.github/workflows/clang-tidy-sweep.yml`** — either delete (GCC has no equivalent) or rewrite to use system clang-tidy without the clang-p2996 fork.
- **Docs:** `CLAUDE.md`, `docs/development/GETTING_STARTED.md`, `docs/development/COMPILER_ISSUES.md`, `docs/development/FUZZING.md`, `docs/development/SANITIZERS.md`, `docs/development/PRE_COMMIT.md`, `docs/development/CPP26_CODING_STANDARDS.md`, `docs/README.md`, `docs/architecture/DESIGN_DECISIONS.md`, `docs/features/JOIN_OPERATIONS.md`.
- **Agent files:** every file under `.claude/agents/` that mentions clang-p2996 / LIBCXX_ROOT.

---

## Phase 0: Reset working tree

The current `feature/226-gcc-build-support` branch carries 79 modified files from a prior exploratory session. Reset to clean develop state before starting.

### Task 0.1: Reset branch to develop

**Files:** Working tree (all files).

- [ ] **Step 1: Confirm we're on the right branch and stash isn't needed**

```bash
git status --short | head -5
git branch --show-current
```

Expected: branch is `feature/226-gcc-build-support`, working tree shows modified files.

- [ ] **Step 2: Hard-restore to develop**

```bash
git fetch origin develop
git reset --hard origin/develop
```

Expected: working tree clean, branch tip matches `origin/develop`.

- [ ] **Step 3: Verify clean baseline build (Clang, the way it was)**

```bash
cmake --preset ninja-debug && cmake --build --preset ninja-debug -- -j4
```

Expected: build succeeds. This is the regression oracle — anything that builds here must still build (or have a documented replacement) at the end of the plan.

- [ ] **Step 4: Commit a marker on a new sub-branch so we can compare**

```bash
git checkout -b feature/226-gcc-only-baseline
git push -u origin feature/226-gcc-only-baseline
git checkout feature/226-gcc-build-support
```

This is bookkeeping only — gives us a "before" reference. No code commit.

---

## Phase 1: CMake purge — drop clang-p2996 infrastructure

Strip every clang-p2996 / libc++ reference from the build system. After this phase, configure under GCC works but build will fail until Phase 2/3.

### Task 1.1: Add the import-std experimental gate

**Files:**
- Modify: `CMakeLists.txt` (insert before `project()`)

- [ ] **Step 1: Open CMakeLists.txt and add the gate UUID before `project(storm)`**

Insert after `cmake_minimum_required(VERSION 3.30)`:

```cmake
# CMake's experimental `import std;` support gate. Token re-rolls as the feature
# evolves; current value valid for CMake 3.30–4.x. Must be set before project().
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
    "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")
```

- [ ] **Step 2: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: enable CMake experimental import-std gate (#226)"
```

### Task 1.2: Hard-pin compiler to GCC ≥ 16

**Files:**
- Modify: `CMakeLists.txt` lines 10-25 (the compiler-gate block)

- [ ] **Step 1: Replace the dual-compiler block with GCC-only**

In `CMakeLists.txt`, replace:

```cmake
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "21")
    message(
      FATAL_ERROR "Clang >= 21 required. Got: ${CMAKE_CXX_COMPILER_VERSION}")
  endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "16")
    message(
      FATAL_ERROR "GCC >= 16 required. Got: ${CMAKE_CXX_COMPILER_VERSION}")
  endif()
else()
  message(
    FATAL_ERROR
      "This project requires Clang >= 21 or GCC >= 16. Got: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
  )
endif()
```

with:

```cmake
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(
    FATAL_ERROR "This project requires GCC. Got: ${CMAKE_CXX_COMPILER_ID}")
endif()
if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "16")
  message(
    FATAL_ERROR "GCC >= 16 required. Got: ${CMAKE_CXX_COMPILER_VERSION}")
endif()
```

- [ ] **Step 2: Replace Clang-conditioned global compile options**

Find this block:

```cmake
add_compile_options(
  $<$<CXX_COMPILER_ID:Clang>:-ferror-limit=1>
  $<$<CXX_COMPILER_ID:GNU>:-fmax-errors=1>
  $<$<AND:$<CXX_COMPILER_ID:Clang>,$<CONFIG:Debug>>:-ftemplate-backtrace-limit=0>)

# Pin Clang's module cache to the build dir. [long comment]
add_compile_options(
  $<$<CXX_COMPILER_ID:Clang>:-fmodules-cache-path=${CMAKE_BINARY_DIR}/module-cache>
)
```

(If the working tree is freshly reset, the original on develop is:
```cmake
add_compile_options(-ferror-limit=1
                    $<$<CONFIG:Debug>:-ftemplate-backtrace-limit=0>)
add_compile_options("-fmodules-cache-path=${CMAKE_BINARY_DIR}/module-cache")
```)

Replace either form with:

```cmake
add_compile_options(-fmax-errors=1)
# GCC writes module BMIs to gcm.cache/ in the build dir automatically — no
# equivalent of clang's -fmodules-cache-path is needed.
```

- [ ] **Step 3: Drop the libcxx include**

Find and delete:

```cmake
include(cmake/libcxx.cmake)
```

- [ ] **Step 4: Replace `apply_cxx_flags()` call with inline target flags**

`apply_cxx_flags` was defined in libcxx.cmake. Since it now does only one thing (apply `-fmodules -freflection` + `CXX_MODULE_STD ON`), inline it. Find:

```cmake
apply_cxx_flags(${PROJECT_NAME})
```

Replace with:

```cmake
target_compile_options(${PROJECT_NAME} PRIVATE -fmodules -freflection)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_MODULE_STD ON)
```

NOTE: there are 9 other call sites of `apply_cxx_flags` — Task 1.4 handles those by re-introducing the helper as a tiny GCC-only function in a new place.

- [ ] **Step 5: Verify config still parses (build will fail later)**

```bash
rm -rf build/debug && CC=gcc CXX=g++ cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON 2>&1 | tail -10
```

Expected: configure completes with `=== Storm Build Configuration ===` banner. Build target list is generated.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: hard-pin GCC, drop clang flags + libcxx include (#226)"
```

### Task 1.3: Delete `cmake/libcxx.cmake`

**Files:**
- Delete: `cmake/libcxx.cmake`

- [ ] **Step 1: Delete the file**

```bash
git rm cmake/libcxx.cmake
```

- [ ] **Step 2: Verify nothing in `cmake/` still references it**

```bash
grep -rn "libcxx\|LIBCXX_ROOT" cmake/ CMakeLists.txt
```

Expected: zero matches.

- [ ] **Step 3: Commit**

```bash
git commit -m "build: remove cmake/libcxx.cmake — libstdc++ needs no per-target wiring (#226)"
```

### Task 1.4: Reintroduce `apply_cxx_flags()` in `cmake/gcc_flags.cmake`

`apply_cxx_flags` is called from 9 targets across `tests/`, `benchmarks/`, `fuzz/`, `cmake/storm_migrations.cmake`. Replacing each call site is more diff than keeping the helper. We re-add it in a tiny GCC-only file.

**Files:**
- Create: `cmake/gcc_flags.cmake`
- Modify: `CMakeLists.txt` (replace the inlined block from Task 1.2 with `include(cmake/gcc_flags.cmake)` + `apply_cxx_flags(${PROJECT_NAME})`)

- [ ] **Step 1: Create cmake/gcc_flags.cmake**

```cmake
# Per-target reflection / modules flags for GCC.
# GCC 16+ bundles annotation parsing and expansion statements into
# `-freflection`, so no separate `-fannotation-attributes` /
# `-fexpansion-statements` flags are needed (those were clang-p2996 fork flags).
function(apply_cxx_flags target_name)
  target_compile_options(${target_name} PRIVATE -fmodules -freflection)
  # GCC has no built-in std module map. Opt in to CMake's experimental
  # CXX_MODULE_STD so it auto-builds libstdc++.modules.json before consumers
  # see `import std;`.
  set_target_properties(${target_name} PROPERTIES CXX_MODULE_STD ON)
endfunction()
```

- [ ] **Step 2: Wire it from CMakeLists.txt**

Replace the inlined block added in Task 1.2 Step 4:

```cmake
target_compile_options(${PROJECT_NAME} PRIVATE -fmodules -freflection)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_MODULE_STD ON)
```

with:

```cmake
include(cmake/gcc_flags.cmake)
apply_cxx_flags(${PROJECT_NAME})
```

- [ ] **Step 3: Verify configure still works**

```bash
rm -rf build/debug && CC=gcc CXX=g++ cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON 2>&1 | grep -E "Storm Build|FATAL|Error"
```

Expected: `=== Storm Build Configuration ===` line, no FATAL/Error.

- [ ] **Step 4: Commit**

```bash
git add cmake/gcc_flags.cmake CMakeLists.txt
git commit -m "build: extract GCC apply_cxx_flags() into cmake/gcc_flags.cmake (#226)"
```

### Task 1.5: Update `cmake/coverage.cmake` to use system gcov/lcov

**Files:**
- Modify: `cmake/coverage.cmake` lines 22-23

- [ ] **Step 1: Read the current file to confirm the lines**

```bash
sed -n '18,30p' cmake/coverage.cmake
```

You'll see something like:

```cmake
if(ENABLE_COVERAGE)
  set(LLVM_PROFDATA "${LIBCXX_ROOT}/build/bin/llvm-profdata")
  set(LLVM_COV "${LIBCXX_ROOT}/build/bin/llvm-cov")
```

- [ ] **Step 2: Replace with gcov-based equivalents**

```cmake
if(ENABLE_COVERAGE)
  find_program(GCOV gcov REQUIRED)
  find_program(LCOV lcov REQUIRED)
  find_program(GENHTML genhtml REQUIRED)
```

- [ ] **Step 3: Update `cmake/coverage-targets.cmake` to use these variables**

Read the file: `sed -n '1,50p' cmake/coverage-targets.cmake`. Replace any `${LLVM_PROFDATA}` / `${LLVM_COV}` invocations with the gcov/lcov equivalents:

- LLVM workflow was: compile with `-fprofile-instr-generate -fcoverage-mapping`, run, `llvm-profdata merge`, `llvm-cov export`.
- GCC workflow is: compile with `--coverage`, run, `lcov --capture`, `genhtml`.

A complete replacement is in scope for this task — keep the target names (`coverage`, `coverage-html`, `coverage-clean`) the same so `commit.sh` and CI keep working. Concrete contents:

```cmake
add_custom_target(coverage-clean
  COMMAND find ${CMAKE_BINARY_DIR} -name "*.gcda" -delete
  COMMENT "Removing existing .gcda coverage data")

add_custom_target(coverage
  DEPENDS storm_tests
  COMMAND ctest --output-on-failure
  COMMAND ${LCOV} --capture --directory ${CMAKE_BINARY_DIR}
                  --output-file ${CMAKE_BINARY_DIR}/coverage.info
                  --rc geninfo_unexecuted_blocks=1 --ignore-errors mismatch
  COMMAND ${LCOV} --extract ${CMAKE_BINARY_DIR}/coverage.info
                  "${CMAKE_SOURCE_DIR}/src/*"
                  --output-file ${CMAKE_BINARY_DIR}/coverage.filtered.info
  COMMAND ${LCOV} --summary ${CMAKE_BINARY_DIR}/coverage.filtered.info
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running tests and generating coverage summary")

add_custom_target(coverage-html
  DEPENDS coverage
  COMMAND ${GENHTML} ${CMAKE_BINARY_DIR}/coverage.filtered.info
                     --output-directory ${CMAKE_BINARY_DIR}/coverage/html-filtered
                     --legend --title "Storm Coverage"
  COMMENT "Generating HTML coverage report at coverage/html-filtered/index.html")
```

- [ ] **Step 4: Add `--coverage` to coverage builds**

In `cmake/coverage.cmake`, after the find_programs, add:

```cmake
add_compile_options($<$<BOOL:${ENABLE_COVERAGE}>:--coverage>)
add_link_options($<$<BOOL:${ENABLE_COVERAGE}>:--coverage>)
```

- [ ] **Step 5: Commit**

```bash
git add cmake/coverage.cmake cmake/coverage-targets.cmake
git commit -m "build: replace llvm-cov coverage with gcov+lcov (#226)"
```

### Task 1.6: Fix the test-target constexpr flag

**Files:**
- Modify: `tests/CMakeLists.txt` lines 78-80

- [ ] **Step 1: Replace `-fconstexpr-steps` with the GCC equivalent**

In `tests/CMakeLists.txt`, find:

```cmake
target_compile_options(${PROJECT_NAME} PRIVATE -fconstexpr-steps=4194304)
```

Replace with:

```cmake
# GCC's -fconstexpr-ops-limit counts individual operations (not steps), so
# the limit is much higher. Tuned for the consteval JSON parser that drives
# 247 YAML test cases.
target_compile_options(${PROJECT_NAME} PRIVATE -fconstexpr-ops-limit=134217728)
```

- [ ] **Step 2: Commit**

```bash
git add tests/CMakeLists.txt
git commit -m "test: switch constexpr-steps to GCC constexpr-ops-limit (#226)"
```

### Task 1.7: Drop fuzz target's clang-specific flag

**Files:**
- Modify: `fuzz/CMakeLists.txt` line 14

- [ ] **Step 1: Read fuzz/CMakeLists.txt**

```bash
sed -n '10,25p' fuzz/CMakeLists.txt
```

If there's a `-fmodules-cache-path=...` line, delete it. The `apply_cxx_flags(${name})` call stays.

Also check for any `-fsanitize=fuzzer` usage — GCC supports `-fsanitize=address,undefined` natively but not `-fsanitize=fuzzer` (libFuzzer is clang-only). If fuzz targets use `-fsanitize=fuzzer`, mark them as a follow-up issue and leave them compiling-but-skipped via:

```cmake
if(ENABLE_FUZZING)
  message(WARNING "Fuzzing targets require libFuzzer (clang-only); disabled under GCC")
  return()
endif()
```

at the top of `fuzz/CMakeLists.txt`. Document in a TODO comment that this needs revisiting.

- [ ] **Step 2: Commit**

```bash
git add fuzz/CMakeLists.txt
git commit -m "fuzz: gate libFuzzer targets behind clang requirement (#226)"
```

### Task 1.8: Phase-1 smoke check

- [ ] **Step 1: Re-configure cleanly**

```bash
rm -rf build/debug && CC=gcc CXX=g++ cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_COVERAGE=ON 2>&1 | tee /tmp/phase1-configure.log | grep -E "Storm Build|FATAL|Error" | head -10
```

Expected: `=== Storm Build Configuration ===` present, `Compiler: GNU 16.1.1`, no FATAL/Error lines.

- [ ] **Step 2: Attempt a build to confirm the failure mode is "source-level, not build-system"**

```bash
cmake --build build/debug -- -j1 2>&1 | tail -10
```

Expected: failure at a `.cppm` scan/compile step with errors like `failed to read compiled module: …expected.gcm` or `'size_t' was not declared`. These are real source-level issues, **not CMake bugs**. If you see "unrecognized command-line option" or "file not found: libcxx", Phase 1 is incomplete — fix before proceeding.

---

## Phase 2: Source-level migration — `import std;` and `using std::…;`

Replace all 24 `.cppm` source files' header units with `import std;` and inject `using std::…;` declarations for bare typedefs. Same for the 45 test/bench files.

### Task 2.1: Run the header-unit replacement script

**Files:**
- Modify: 24 files under `src/**/*.cppm`
- Modify: 45 files under `tests/`, `benchmarks/`, `tools/`, `fuzz/` (any `*.cppm`/`*.cpp`/`*.hpp` with header-unit imports)

- [ ] **Step 1: Replace `import <foo>;` with `import std;` in src/**

Run this from the repo root:

```bash
for f in $(grep -rln '^import\s*<' src/ --include='*.cppm'); do
  python3 -c "
import re, pathlib, sys
p = pathlib.Path('$f')
text = p.read_text()
imports = re.findall(r'^import\s*<[^>]+>\s*;', text, flags=re.M)
if not imports:
    sys.exit(0)
new = re.sub(r'^import\s*<[^>]+>\s*;', 'import std;', text, count=1, flags=re.M)
new = re.sub(r'^import\s*<[^>]+>\s*;\n', '', new, flags=re.M)
p.write_text(new)
print(f'{p}: replaced {len(imports)} header units with import std;')
"
done
```

Expected output: 24 lines, one per file, showing the per-file count of replaced imports.

- [ ] **Step 2: Same for tests/benchmarks/tools/fuzz**

```bash
for f in $(grep -rln '^import\s*<' tests/ benchmarks/ tools/ fuzz/ --include='*.cppm' --include='*.cpp' --include='*.hpp' 2>/dev/null); do
  python3 -c "
import re, pathlib, sys
p = pathlib.Path('$f')
text = p.read_text()
imports = re.findall(r'^import\s*<[^>]+>\s*;', text, flags=re.M)
if not imports:
    sys.exit(0)
new = re.sub(r'^import\s*<[^>]+>\s*;', 'import std;', text, count=1, flags=re.M)
new = re.sub(r'^import\s*<[^>]+>\s*;\n', '', new, flags=re.M)
p.write_text(new)
print(f'{p}: replaced {len(imports)} header units with import std;')
"
done
```

- [ ] **Step 3: Verify zero header units remain**

```bash
grep -rn '^import\s*<' src/ tests/ benchmarks/ tools/ fuzz/ 2>/dev/null | head -5
```

Expected: zero matches.

- [ ] **Step 4: Commit src/ changes**

```bash
git add src/
git commit -m "modules: replace header-unit imports with \`import std;\` in src/ (#226)"
```

- [ ] **Step 5: Commit tests/bench/tools/fuzz changes**

```bash
git add tests/ benchmarks/ tools/ fuzz/
git commit -m "modules: replace header-unit imports with \`import std;\` in tests/bench (#226)"
```

### Task 2.2: Drop textual std-header includes that clash with `import std;`

**Files:**
- Modify: `src/db/pool.cppm` lines 6-7 (remove `#include <condition_variable>` and `#include <mutex>`)
- Modify: `src/orm/statements/distinct.cppm` line 7 (remove `#include <utility>`)

These were the only two cases observed during exploration. The textual include + `import std;` combination produces `__mbstate_t` conflicts under GCC because the C-libc headers transitively pulled in by `<condition_variable>` re-declare types that `import std;` also imports.

- [ ] **Step 1: Edit `src/db/pool.cppm`**

Find:

```cpp
module;

// LINT-EXCLUDE-FILE: duplicate, complexity, length
// Boilerplate-pattern duplicates accepted (see #264 finding).

#include <condition_variable>
#include <mutex>

export module storm_db_pool;
```

Replace with:

```cpp
module;

// LINT-EXCLUDE-FILE: duplicate, complexity, length
// Boilerplate-pattern duplicates accepted (see #264 finding).

export module storm_db_pool;
```

- [ ] **Step 2: Edit `src/orm/statements/distinct.cppm`**

Find:

```cpp
#include <meta>
#include <utility>
#include <plf_hive/plf_hive.h>
```

Replace with:

```cpp
#include <meta>
#include <plf_hive/plf_hive.h>
```

(Note: `plf_hive/plf_hive.h` stays for now — it's a third-party non-std header. It moves to `storm_c_headers` in Phase 3. `<meta>` stays — it's the reflection header that must be textually included.)

- [ ] **Step 3: Verify no other module unit has `#include <std-header>` after `module;`**

```bash
grep -rEhn '^#include\s*<(array|chrono|concepts|coroutine|cstddef|cstdint|cstdio|cstdlib|exception|expected|filesystem|format|functional|iterator|memory|new|optional|random|ranges|span|sstream|string|string_view|tuple|type_traits|unordered_map|unordered_set|utility|variant|vector|condition_variable|mutex|thread|atomic|future)>' src/ --include='*.cppm'
```

Expected: zero matches (after this task's edits).

- [ ] **Step 4: Commit**

```bash
git add src/db/pool.cppm src/orm/statements/distinct.cppm
git commit -m "modules: drop textual std-header includes that conflict with import std (#226)"
```

### Task 2.3: Inject `using std::…;` declarations for bare typedefs

`import std;` doesn't leak names into the global namespace the way Clang's header units did. 261 sites use bare `size_t`, plus 32 `int64_t`, 9 `uint8_t`, 3 `uint64_t`, 1 `ptrdiff_t`. Auto-inject per-file `using std::…;` blocks right after `import std;`.

**Files:**
- Modify: 19 `.cppm` files in `src/` (the subset that uses bare typedefs)

- [ ] **Step 1: Run the injection script**

From repo root:

```bash
python3 - <<'EOF'
import re, pathlib
from collections import Counter
TYPES = ('size_t', 'ptrdiff_t', 'int8_t', 'int16_t', 'int32_t', 'int64_t',
         'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t', 'nullptr_t')
pat = re.compile(r'\b(' + '|'.join(TYPES) + r')\b')
qpat = re.compile(r'std::(' + '|'.join(TYPES) + r')')

for f in sorted(pathlib.Path('src').rglob('*.cppm')):
    txt = f.read_text()
    bare = Counter(pat.findall(txt)) - Counter(qpat.findall(txt))
    if not bare:
        continue
    needed = sorted(bare.keys())
    using_lines = '\n'.join(f'using std::{t};' for t in needed)
    block = '\n' + using_lines + '\n'
    if any(f'using std::{t};' in txt for t in needed):
        # already injected; skip
        continue
    new = re.sub(r'(^import std;\s*\n)', r'\1' + block, txt, count=1, flags=re.M)
    if new == txt:
        print(f'!! WARNING: no `import std;` line found in {f}')
        continue
    f.write_text(new)
    print(f'{f}: added using std::{{ {", ".join(needed)} }}')
EOF
```

Expected: ~19 lines of output, each naming a file plus the types it needed.

- [ ] **Step 2: Spot-check the injection**

```bash
sed -n '1,12p' src/db/concept.cppm
```

Expected output:

```cpp
module;

export module storm_db_concept;
import std;

using std::size_t;
export namespace storm::db {
…
```

- [ ] **Step 3: Run the same script over tests/bench (smaller impact but safer to cover)**

```bash
python3 - <<'EOF'
import re, pathlib
from collections import Counter
TYPES = ('size_t', 'ptrdiff_t', 'int8_t', 'int16_t', 'int32_t', 'int64_t',
         'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t', 'nullptr_t')
pat = re.compile(r'\b(' + '|'.join(TYPES) + r')\b')
qpat = re.compile(r'std::(' + '|'.join(TYPES) + r')')

for f in sorted(list(pathlib.Path('tests').rglob('*.cpp')) +
                list(pathlib.Path('tests').rglob('*.hpp')) +
                list(pathlib.Path('benchmarks').rglob('*.cppm')) +
                list(pathlib.Path('benchmarks').rglob('*.cpp')) +
                list(pathlib.Path('benchmarks').rglob('*.hpp'))):
    txt = f.read_text()
    bare = Counter(pat.findall(txt)) - Counter(qpat.findall(txt))
    if not bare:
        continue
    needed = sorted(bare.keys())
    using_lines = '\n'.join(f'using std::{t};' for t in needed)
    block = '\n' + using_lines + '\n'
    if any(f'using std::{t};' in txt for t in needed):
        continue
    new = re.sub(r'(^import std;\s*\n)', r'\1' + block, txt, count=1, flags=re.M)
    if new == txt:
        continue  # no import std (probably a header that uses std:: directly)
    f.write_text(new)
    print(f'{f}: added using std::{{ {", ".join(needed)} }}')
EOF
```

Expected: variable count of lines; may be empty if tests use `std::` qualification consistently.

- [ ] **Step 4: Commit src/ injections**

```bash
git add src/
git commit -m "modules: add using std::{size_t,...} after import std in src/ (#226)"
```

- [ ] **Step 5: Commit tests/bench injections (if any)**

```bash
git add tests/ benchmarks/ 2>/dev/null && git diff --cached --quiet || git commit -m "modules: add using std::{size_t,...} after import std in tests/bench (#226)"
```

### Task 2.4: Phase-2 smoke check

- [ ] **Step 1: Re-build and see how far we get**

```bash
rm -rf build/debug && CC=gcc CXX=g++ cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON 2>&1 | tail -5
cmake --build build/debug -- -j1 2>&1 | tee /tmp/phase2-build.log | grep -E "FAILED|error:" | head -10
```

Expected: build progresses past stdlib BMI (`std.cc` builds), past `concept.cppm`, but fails at one of the C-header-including modules (`postgresql_statement.cppm`, `sqlite.cppm`, etc.) with `__mbstate_t` conflict errors. **This is the wall Phase 3 solves — don't fix it here.**

If the build gets further than expected (e.g., the C-header conflict doesn't fire), great; verify by running `cmake --build build/debug -- -j1 2>&1 | tail -5` and proceed to Phase 3 anyway because the other files will hit it.

---

## Phase 3: The `storm_c_headers` module — isolate C-system headers

This is the one new design piece. Create a single module that owns all C-system header `#include`s in its global-module fragment, exports the needed types/functions via `using ::name;`, and is consumed by every module that previously included those C headers.

The pattern was validated 2026-05-19 — a separate prototype at `/tmp/c-mod-probe/` compiled cleanly with `import storm_c_headers;` from a TU that also did `import std;`.

### Task 3.1: Create `src/storm_c_headers.cppm`

**Files:**
- Create: `src/storm_c_headers.cppm`

The list of names to re-export was determined by grepping the existing source for symbols that originate in these C headers. To stay surgical and avoid blanket re-exports, only export what Storm uses.

- [ ] **Step 1: Survey what Storm uses from each C header**

Run these to inventory:

```bash
echo "--- sqlite3 symbols used:"
grep -roh '\bsqlite3_[a-z_]*\b\|\bSQLITE_[A-Z_]*\b' src/ | sort -u | head -40
echo "--- libpq symbols used:"
grep -roh '\bPQ[A-Z][A-Za-z_]*\b\|\bPGconn\|\bPGresult\|\bExecStatusType\|\bConnStatusType' src/ | sort -u | head -40
echo "--- uuid (stduuid) symbols:"
grep -roh '\buuids::[a-zA-Z_]*\b' src/ | sort -u | head -20
```

- [ ] **Step 2: Write the module**

Save as `src/storm_c_headers.cppm`:

```cpp
// SPDX-License-Identifier: MIT
// storm_c_headers.cppm
//
// Isolates C-system headers from the rest of Storm's module graph.
//
// Under GCC, including a C header (which transitively pulls in <stdio.h>,
// <wchar.h>, etc.) from a translation unit that also does `import std;`
// produces redeclaration errors for libc typedefs like __mbstate_t (GCC sees
// the same typedef once as a global declaration via the C header and once as
// a module-attached declaration via `import std;`).
//
// The fix is to confine all C-header inclusion to ONE module unit that does
// NOT `import std;`. Other modules `import storm_c_headers;` instead of
// `#include`-ing the C headers, and the conflict never materialises.

module;

#include <sqlite3.h>
#include <libpq-fe.h>
#include <uuid.h>
#include <plf_hive/plf_hive.h>

export module storm_c_headers;

// Re-export the C symbols we use. Using `using ::name;` keeps them in the
// global namespace, matching the original `#include` behavior.
export {
    // SQLite3 ---------------------------------------------------------------
    using ::sqlite3;
    using ::sqlite3_stmt;
    using ::sqlite3_open;
    using ::sqlite3_open_v2;
    using ::sqlite3_close;
    using ::sqlite3_close_v2;
    using ::sqlite3_prepare_v2;
    using ::sqlite3_finalize;
    using ::sqlite3_step;
    using ::sqlite3_reset;
    using ::sqlite3_bind_int;
    using ::sqlite3_bind_int64;
    using ::sqlite3_bind_double;
    using ::sqlite3_bind_text;
    using ::sqlite3_bind_blob;
    using ::sqlite3_bind_null;
    using ::sqlite3_column_int;
    using ::sqlite3_column_int64;
    using ::sqlite3_column_double;
    using ::sqlite3_column_text;
    using ::sqlite3_column_bytes;
    using ::sqlite3_column_blob;
    using ::sqlite3_column_type;
    using ::sqlite3_column_count;
    using ::sqlite3_errmsg;
    using ::sqlite3_exec;
    using ::sqlite3_changes;
    using ::sqlite3_last_insert_rowid;
    using ::sqlite3_clear_bindings;

    // libpq -----------------------------------------------------------------
    using ::PGconn;
    using ::PGresult;
    using ::ConnStatusType;
    using ::ExecStatusType;
    using ::Oid;
    using ::PQconnectdb;
    using ::PQfinish;
    using ::PQstatus;
    using ::PQerrorMessage;
    using ::PQexec;
    using ::PQexecParams;
    using ::PQprepare;
    using ::PQexecPrepared;
    using ::PQresultStatus;
    using ::PQresultErrorMessage;
    using ::PQgetvalue;
    using ::PQgetisnull;
    using ::PQntuples;
    using ::PQnfields;
    using ::PQclear;
    using ::PQescapeLiteral;
    using ::PQfreemem;
    using ::PQcmdTuples;

    // plf::hive -------------------------------------------------------------
    // plf_hive lives in `namespace plf` — re-export the whole namespace.
}

// plf_hive is template-heavy and namespaced — re-export by alias.
export namespace plf {
    using ::plf::hive;
}
```

NOTE: The full symbol list above is a starting point — Step 1's grep may surface additional names Storm uses. Add them in alphabetical groups; do NOT add anything Storm doesn't actually reference (YAGNI).

The `uuid.h` (stduuid) header is C++-only and namespaced (`uuids::`). Re-export its namespace too:

Append after the `plf` namespace:

```cpp
export namespace uuids {
    using ::uuids::uuid;
    using ::uuids::uuid_random_generator;
    using ::uuids::uuid_system_generator;
    using ::uuids::to_string;
}
```

- [ ] **Step 3: Verify the new module compiles in isolation**

```bash
CC=gcc CXX=g++ cmake --build build/debug --target storm -- -j1 2>&1 | grep -E "storm_c_headers|FAILED|error" | head -10
```

Expected: `storm_c_headers.cppm.o` builds before any of its consumers. If it fails, look at the error and ensure all `using ::name;` declarations refer to names actually declared by the C headers (some PQ functions may have been removed/renamed across libpq versions).

- [ ] **Step 4: Commit**

```bash
git add src/storm_c_headers.cppm
git commit -m "modules: add storm_c_headers to isolate C headers from import std (#226)"
```

### Task 3.2: Swap C-header includes for `import storm_c_headers;`

**Files:**
- Modify: `src/db/postgresql_connection.cppm`
- Modify: `src/db/postgresql_statement.cppm`
- Modify: `src/db/sqlite.cppm`
- Modify: `src/orm/utilities.cppm`
- Modify: `src/orm/queryset.cppm`
- Modify: `src/orm/statements/aggregate.cppm`
- Modify: `src/orm/statements/distinct.cppm`
- Modify: `src/orm/statements/select.cppm`
- Modify: `src/orm/statements/setop.cppm`

- [ ] **Step 1: Process each file**

For each file in the list above, do the following manually (one file at a time, so you can verify):

1. Read the top of the file (`head -20 <file>`).
2. Identify the `#include <…>` line(s) for C headers (`<libpq-fe.h>`, `<sqlite3.h>`, `<uuid.h>`, `<plf_hive/plf_hive.h>`).
3. Delete those `#include` lines.
4. After the existing `export module …;` and any `import …;` lines, add `import storm_c_headers;`.

Concrete example for `src/db/sqlite.cppm`:

Before:
```cpp
module;

// LINT-EXCLUDE-FILE: duplicate
// SQLite driver — pattern duplicates with postgresql driver are expected.

#include <sqlite3.h>

export module storm_db_sqlite;
import storm_db_concept;
import std;

using std::size_t;
using std::int64_t;
```

After:
```cpp
module;

// LINT-EXCLUDE-FILE: duplicate
// SQLite driver — pattern duplicates with postgresql driver are expected.

export module storm_db_sqlite;
import storm_db_concept;
import storm_c_headers;
import std;

using std::size_t;
using std::int64_t;
```

- [ ] **Step 2: Verify the swap is complete**

```bash
grep -rEn '^#include\s*<(libpq-fe\.h|sqlite3\.h|uuid\.h|plf_hive/plf_hive\.h)>' src/ --include='*.cppm'
```

Expected: zero matches.

- [ ] **Step 3: Verify all 9 files now `import storm_c_headers;`**

```bash
grep -rEn '^import\s+storm_c_headers;' src/ --include='*.cppm' | wc -l
```

Expected: 9.

- [ ] **Step 4: Commit**

```bash
git add src/
git commit -m "modules: replace C header includes with import storm_c_headers (#226)"
```

### Task 3.3: Phase-3 build-green check

- [ ] **Step 1: Full rebuild**

```bash
rm -rf build/debug && CC=gcc CXX=g++ cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON 2>&1 | tail -5
cmake --build build/debug -- -j1 2>&1 | tee /tmp/phase3-build.log | tail -20
```

Expected: build completes through the entire `storm` library and into tests. If a `.cppm` fails with `error: '<name>' was not declared` for a C-API symbol (e.g., `sqlite3_prepare_v3`), add the missing `using ::name;` to `storm_c_headers.cppm` and rebuild.

- [ ] **Step 2: If build fails on a non-C-header issue, capture it**

```bash
grep -E "error:" /tmp/phase3-build.log | head -5
```

Triage: each unique error type either gets a fix here (if it's a simple use-decl miss, namespace qualification, or include order) or gets logged as a follow-up task. **Do not let unrelated errors block Phase 3.** If more than 3 distinct error types appear, stop and report — the plan may need a Phase 3.5 to handle them.

- [ ] **Step 3: Confirm libstorm.a exists**

```bash
ls -la build/debug/libstorm.a
```

Expected: file exists, non-zero size.

- [ ] **Step 4: Commit any straggler fixes from Step 2**

```bash
git add -A && git diff --cached --quiet || git commit -m "build: address residual GCC build errors after C-header isolation (#226)"
```

---

## Phase 4: Tests green — get `ctest` passing

The library builds. Now make tests build and pass.

### Task 4.1: Build the test binary

- [ ] **Step 1: Build only the tests target**

```bash
cmake --build build/debug --target storm_tests -- -j1 2>&1 | tee /tmp/phase4-tests-build.log | grep -E "FAILED|error:" | head -10
```

Expected: tests compile and link. If errors occur, common culprits and fixes:

- **`error: 'std::xxx' has not been declared`** — a `using std::xxx;` is missing in the test file. Add it after `import std;`.
- **`error: undefined reference to 'XYZ'`** at link time — usually a `using ::name;` missing from `storm_c_headers.cppm`. Add it and rebuild.
- **`error: module 'foo' not found`** — a test file is `import`-ing a module that no longer exists or was renamed. Update the import.

For each error class encountered, fix it across all affected files in one commit, then re-build.

- [ ] **Step 2: Loop until tests build clean**

When `cmake --build build/debug --target storm_tests -- -j4 2>&1 | grep -E "FAILED|error:"` is empty, proceed.

- [ ] **Step 3: Commit any test-source fixes**

```bash
git add tests/ && git diff --cached --quiet || git commit -m "test: fix GCC-specific source issues exposed by import std migration (#226)"
```

### Task 4.2: Run the SQLite-only test suite first

PostgreSQL tests need a running PG instance. Start with SQLite to isolate "tests pass" from "PG environment is set up."

- [ ] **Step 1: Run with PostgreSQL disabled**

```bash
STORM_PG_CONNSTR= ./build/debug/tests/storm_tests --gtest_filter='-*Postgres*:*PG*' 2>&1 | tail -25
```

Expected: `[  PASSED  ]` line with a non-zero test count, no failures.

Common SQLite-side failures and triage:

- **`std::format` produces different output** — libstdc++'s `std::format` strictly follows the spec; clang-p2996's libc++ had quirks. Adjust the expected string in the test.
- **`std::vector` ordering differences in hash-based tests** — libstdc++'s `std::unordered_map` has different bucket layout than libc++'s. Sort results before comparing, or assert membership rather than position.
- **Reflection result ordering** — `std::meta::nonstatic_data_members_of` should be deterministic, but if a test relies on a specific order across compilers, verify.

For each failure, capture the test name, diagnose with `gtest_filter=<name> --gtest_break_on_failure`, fix, and move on.

- [ ] **Step 2: When SQLite tests pass, commit any test-side fixes**

```bash
git add tests/ && git diff --cached --quiet || git commit -m "test: stabilize SQLite test suite under libstdc++ (#226)"
```

### Task 4.3: Run PostgreSQL tests

- [ ] **Step 1: Verify PG is reachable**

```bash
psql 'host=/var/run/postgresql dbname=storm_db user=storm_db' -c 'SELECT 1' 2>&1 | head -3
```

If PG is not running, start it before proceeding:

```bash
sudo systemctl start postgresql
```

- [ ] **Step 2: Run the full suite with PG enabled**

```bash
STORM_PG_CONNSTR='host=/var/run/postgresql dbname=storm_db user=storm_db' ctest --test-dir build/debug --output-on-failure 2>&1 | tail -30
```

Expected: `100% tests passed` line.

- [ ] **Step 3: For each PG-specific failure, triage**

Likely culprits:

- **`PQexec` errors with `unrecognized status code`** — a `using ::ConnStatusType;` or similar may be missing from `storm_c_headers.cppm`.
- **Connection-string parsing issues** — libstdc++'s `std::regex` is more strict than libc++'s. May need to relax patterns in `src/db/postgresql_connection.cppm`.

- [ ] **Step 4: When full suite passes, commit**

```bash
git add -A && git diff --cached --quiet || git commit -m "test: stabilize PostgreSQL test suite under GCC (#226)"
```

### Task 4.4: Coverage smoke check

- [ ] **Step 1: Configure with coverage on**

```bash
rm -rf build/debug && CC=gcc CXX=g++ cmake -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_COVERAGE=ON 2>&1 | tail -3
cmake --build build/debug -- -j4
```

- [ ] **Step 2: Run coverage target**

```bash
cmake --build build/debug --target coverage 2>&1 | tail -15
```

Expected: `lcov` runs, produces a summary line showing line/function coverage percentages.

- [ ] **Step 3: Generate HTML report**

```bash
cmake --build build/debug --target coverage-html
ls build/debug/coverage/html-filtered/index.html
```

Expected: index.html exists.

- [ ] **Step 4: If coverage targets work, commit**

Coverage CMake changes were committed in Phase 1; this task only verified them. No new commit unless we had to fix something in `cmake/coverage-targets.cmake`.

### Task 4.5: Release build smoke check

- [ ] **Step 1: Configure and build Release**

```bash
rm -rf build/release && CC=gcc CXX=g++ cmake -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DENABLE_BENCH=ON 2>&1 | tail -3
cmake --build build/release -- -j4 2>&1 | tail -10
```

Expected: builds cleanly. Release builds use `-O3` which can shake out new template instantiation issues that Debug hides.

- [ ] **Step 2: Run tests under Release**

```bash
ctest --test-dir build/release --output-on-failure 2>&1 | tail -10
```

Expected: all tests pass.

- [ ] **Step 3: Smoke-run benchmarks (don't compare against history yet)**

```bash
./build/release/benchmarks/storm_bench --benchmark_filter='Storm/SELECT/Single' 2>&1 | tail -10
```

Expected: benchmark runs to completion. Performance numbers may differ from the documented 96–108% under clang-p2996 + libc++ — that's a separate concern (risk register).

- [ ] **Step 4: Commit anything that needed fixing in Release**

```bash
git add -A && git diff --cached --quiet || git commit -m "build: stabilize Release build under GCC (#226)"
```

---

## Phase 5: Presets — clean up `CMakePresets.json`

`CMakePresets.json` currently has both clang and gcc presets. Drop the clang stuff and make GCC the default.

### Task 5.1: Repoint `ninja-debug` and `ninja-release` at GCC

**Files:**
- Modify: `CMakePresets.json`

- [ ] **Step 1: Read the current presets**

```bash
cat CMakePresets.json
```

- [ ] **Step 2: Replace the `base` configure preset**

Find:

```json
{
  "name": "base",
  "hidden": true,
  "generator": "Ninja",
  "cacheVariables": {
    "CMAKE_C_COMPILER": "${sourceDir}/../clang-p2996/build/bin/clang",
    "CMAKE_CXX_COMPILER": "${sourceDir}/../clang-p2996/build/bin/clang++",
    "CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS": "${sourceDir}/../clang-p2996/build/bin/clang-scan-deps",
    "LIBCXX_ROOT": "${sourceDir}/../clang-p2996",
    "CPM_SOURCE_CACHE": "${sourceDir}/.cache/CPM"
  }
}
```

Replace with:

```json
{
  "name": "base",
  "hidden": true,
  "generator": "Ninja",
  "cacheVariables": {
    "CMAKE_C_COMPILER": "gcc",
    "CMAKE_CXX_COMPILER": "g++",
    "CPM_SOURCE_CACHE": "${sourceDir}/.cache/CPM"
  }
}
```

- [ ] **Step 3: Remove any `gcc-base` / `ninja-gcc-debug` presets that were added during exploration**

If the file has `"name": "gcc-base"` or `"name": "ninja-gcc-debug"` entries (from prior session), delete them — `base` now is GCC and the existing `ninja-debug` inherits it.

- [ ] **Step 4: Configure with the canonical preset**

```bash
rm -rf build/debug && cmake --preset ninja-debug 2>&1 | grep -E "Compiler:|FATAL"
```

Expected: `Compiler: GNU 16.1.1`, no FATAL.

- [ ] **Step 5: Commit**

```bash
git add CMakePresets.json
git commit -m "build: repoint presets at GCC, drop clang-p2996 base preset (#226)"
```

### Task 5.2: Sanitizer / fuzz preset triage

- [ ] **Step 1: Try `ninja-asan-ubsan`**

```bash
rm -rf build/asan-ubsan && cmake --preset ninja-asan-ubsan 2>&1 | tail -5
cmake --build --preset ninja-asan-ubsan -- -j4 2>&1 | tail -10
```

Expected: builds. GCC's `-fsanitize=address,undefined` is supported.

- [ ] **Step 2: Try `ninja-tsan` and `ninja-msan`**

```bash
rm -rf build/tsan && cmake --preset ninja-tsan && cmake --build --preset ninja-tsan -- -j4 2>&1 | tail -5
rm -rf build/msan && cmake --preset ninja-msan && cmake --build --preset ninja-msan -- -j4 2>&1 | tail -10
```

Expected: TSAN builds. **MSAN may fail** — GCC's MSAN equivalent is `-fsanitize=memory` but it requires an MSAN-built libstdc++, which Manjaro doesn't ship. If MSAN fails:

1. Edit `CMakePresets.json` and mark the `ninja-msan` configure preset with `"hidden": true` and a `"description"` line noting "GCC MSAN requires custom libstdc++; disabled".
2. Update `.github/workflows/ci.yml` (Phase 7) to skip the MSAN job.

- [ ] **Step 3: Run tests under each working sanitizer**

```bash
ctest --preset ninja-asan-ubsan 2>&1 | tail -10
ctest --preset ninja-tsan 2>&1 | tail -10
```

- [ ] **Step 4: Commit**

```bash
git add CMakePresets.json && git diff --cached --quiet || git commit -m "build: triage GCC sanitizer preset support (#226)"
```

---

## Phase 6: Tooling — clang-format, clang-tidy, format.cmake

The pre-commit hook (`commit.sh`) runs clang-format and clang-tidy. Both can use system Clang (not clang-p2996) for source-level checks since they don't link against Storm modules — they're text-level linters.

### Task 6.1: Rewrite `cmake/format.cmake` to use system clang-format

**Files:**
- Modify: `cmake/format.cmake`

- [ ] **Step 1: Read the current file**

```bash
cat cmake/format.cmake
```

- [ ] **Step 2: Replace hard-coded clang-p2996 path with `find_program`**

The current file references `${LIBCXX_ROOT}/../clang-p2996/build/bin/clang-format`. Replace with:

```cmake
find_program(CLANG_FORMAT clang-format)
if(NOT CLANG_FORMAT)
  message(STATUS "clang-format not found — format targets disabled")
  return()
endif()
```

Keep the rest of the file (target definitions) unchanged.

- [ ] **Step 3: Verify configure still works**

```bash
rm -rf build/debug && cmake --preset ninja-debug 2>&1 | grep -i format | head -3
```

Expected: either a "found clang-format" status line or a graceful skip.

- [ ] **Step 4: Commit**

```bash
git add cmake/format.cmake
git commit -m "build: switch format.cmake from clang-p2996 to system clang-format (#226)"
```

### Task 6.2: Rewrite `scripts/run_clang_tidy.sh` to use system clang-tidy

**Files:**
- Modify: `scripts/run_clang_tidy.sh`

The script is currently hard-coded to `../clang-p2996/build/bin/clang-tidy` (line 2, 16, 26-27, 30 of the file). Replace those hard-coded paths.

- [ ] **Step 1: Replace the path variables at the top of the script**

Find (near top of file):

```bash
CLANG_TIDY="${SOURCE_DIR}/../clang-p2996/build/bin/clang-tidy"
CLANG_TIDY_DIFF="${SOURCE_DIR}/../clang-p2996/clang-tools-extra/clang-tidy/tool/clang-tidy-diff.py"
```

Replace with:

```bash
CLANG_TIDY="$(command -v clang-tidy)"
CLANG_TIDY_DIFF="$(command -v clang-tidy-diff.py 2>/dev/null || command -v clang-tidy-diff)"

if [[ -z "$CLANG_TIDY" ]]; then
  echo "error: clang-tidy not found in PATH" >&2
  exit 1
fi
```

- [ ] **Step 2: Test the script**

```bash
./scripts/run_clang_tidy.sh --help 2>&1 | head -5
```

Expected: script runs (may print usage). If it dies on a different hard-coded path, fix that line too.

- [ ] **Step 3: Sanity-check it can lint one file**

```bash
./scripts/run_clang_tidy.sh src/storm.cppm 2>&1 | tail -10
```

Expected: clang-tidy runs (may emit warnings/errors; we're only checking the script invokes).

- [ ] **Step 4: Commit**

```bash
git add scripts/run_clang_tidy.sh
git commit -m "build: switch run_clang_tidy.sh from clang-p2996 to system clang-tidy (#226)"
```

### Task 6.3: Update `commit.sh` if it has clang-p2996 paths

**Files:**
- Modify: `commit.sh` (if it references clang-p2996)

- [ ] **Step 1: Check**

```bash
grep -n "clang-p2996\|LIBCXX_ROOT" commit.sh
```

- [ ] **Step 2: If any lines are found, replace clang-p2996 paths the same way as run_clang_tidy.sh**

The pre-commit script calls coverage, tests, clang-tidy. Coverage's invocation may use llvm-cov env vars — make sure those are gone too.

- [ ] **Step 3: Run a full pre-commit check on a no-op commit to verify**

```bash
echo "# test" >> /tmp/probe.txt
git add /tmp/probe.txt 2>&1 || true  # outside repo, expected
# Instead, do a real probe: touch a doc file
echo "" >> docs/README.md
git add docs/README.md
./commit.sh "docs: probe" 2>&1 | tail -10
# Undo
git reset HEAD docs/README.md && git checkout docs/README.md
```

Expected: `commit.sh` runs format + tidy + tests + coverage end-to-end without referencing clang-p2996.

- [ ] **Step 4: Commit any commit.sh changes**

```bash
git add commit.sh && git diff --cached --quiet || git commit -m "build: update commit.sh to use system clang-tools (#226)"
```

---

## Phase 7: CI — drop the `storm-ci` image, use stock GCC

### Task 7.1: Rewrite `.github/workflows/ci.yml`

**Files:**
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Read the current workflow**

```bash
cat .github/workflows/ci.yml
```

- [ ] **Step 2: Replace the runs-on / container block**

Find (around lines 14-16):

```yaml
container:
  image: ghcr.io/spiritecosse/storm-ci@sha256:...
```

Replace the entire `container:` block with:

```yaml
# Run directly on the runner — no custom image needed now that we use stock GCC.
# Manjaro/Arch testing image would also work; ubuntu-24.04 with a GCC PPA is the
# default. Below assumes a GCC 16 install step.
```

Then in the `steps:` block, add a GCC install step before any `cmake` step:

```yaml
- name: Install GCC 16 + tooling
  run: |
    sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
    sudo apt-get update
    sudo apt-get install -y gcc-16 g++-16 ninja-build lcov clang-format clang-tidy postgresql-server-dev-16 libsqlite3-dev
    sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-16 100
    sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-16 100
```

NOTE: if Ubuntu 24.04 doesn't have GCC 16 in any PPA when this work runs, use a self-hosted runner or a custom image. **This is a deployment question** — flag it before the PR merge so the team agrees on the runner story.

- [ ] **Step 3: Remove the clang-p2996 symlink step**

Find (around lines 74-75):

```yaml
- name: Link clang-p2996
  run: ln -sfn /opt/clang-p2996 ../clang-p2996
```

Delete the step entirely.

- [ ] **Step 4: Update preset names if changed**

If Phase 5 renamed presets, update every `--preset ninja-...` line in ci.yml to match.

- [ ] **Step 5: Drop the dead comments referencing clang-scan-deps SIGSEGV**

Find lines 61-66 / 105 and clean up dead clang-fork-specific commentary.

- [ ] **Step 6: Verify the workflow file parses**

```bash
yq eval '.jobs' .github/workflows/ci.yml | head -20
```

(Requires `yq`. If not installed: `python3 -c "import yaml, sys; yaml.safe_load(open('.github/workflows/ci.yml'))" && echo OK`.)

- [ ] **Step 7: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: switch from storm-ci image to stock GCC 16 install (#226)"
```

### Task 7.2: Delete `docker-ci-image.yml` and `clang-tidy-sweep.yml`

The custom image workflow is obsolete. The clang-tidy sweep may be salvageable but needs full rewrite — defer.

**Files:**
- Delete: `.github/workflows/docker-ci-image.yml`
- Modify: `.github/workflows/clang-tidy-sweep.yml` (rewrite or delete)

- [ ] **Step 1: Delete the docker image build workflow**

```bash
git rm .github/workflows/docker-ci-image.yml
```

- [ ] **Step 2: Decide on `clang-tidy-sweep.yml`**

Option A (recommended for this PR): delete it for now and reintroduce as a follow-up issue.

```bash
git rm .github/workflows/clang-tidy-sweep.yml
```

Option B: rewrite to use system clang-tidy. Requires the same path-fixing as `scripts/run_clang_tidy.sh` (Task 6.2). If choosing B, do the path swap, then test end-to-end.

For this plan, **use Option A** — keeps the PR scope manageable.

- [ ] **Step 3: Commit**

```bash
git commit -m "ci: drop clang-p2996 image build + sweep workflows (#226)"
```

---

## Phase 8: Docs and agent files

Update every doc that references clang-p2996, LIBCXX_ROOT, or libc++. The text changes are large but mechanical.

### Task 8.1: Update `CLAUDE.md`

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Find references**

```bash
grep -n "clang-p2996\|LIBCXX_ROOT\|libc++\|libcxx\|llvm-cov\|llvm-profdata" CLAUDE.md
```

- [ ] **Step 2: Edit each occurrence**

- "Custom Clang with C++26 reflection (`../clang-p2996/`)" → "GCC 16+ with `-freflection`"
- `cmake/libcxx.cmake` references → `cmake/gcc_flags.cmake`
- `apply_clang_flags` if mentioned → `apply_cxx_flags`

Open the file in your editor and search/replace these strings. Verify with:

```bash
grep -n "clang-p2996\|LIBCXX_ROOT\|libc++\|libcxx\|llvm-cov\|llvm-profdata" CLAUDE.md
```

Expected: zero matches.

- [ ] **Step 3: Update the "Prerequisites" section**

Find:
```markdown
### Prerequisites
- Custom Clang with C++26 reflection (`../clang-p2996/`)
- SQLite3, CMake 3.30+, Ninja
```

Replace with:
```markdown
### Prerequisites
- GCC 16+ with C++26 reflection (`-freflection` flag)
- SQLite3, libpq, CMake 3.30+, Ninja, lcov (for coverage)
```

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: update CLAUDE.md for GCC-only toolchain (#226)"
```

### Task 8.2: Update `docs/development/COMPILER_ISSUES.md`

**Files:**
- Modify: `docs/development/COMPILER_ISSUES.md`

- [ ] **Step 1: Read it**

```bash
cat docs/development/COMPILER_ISSUES.md
```

- [ ] **Step 2: Replace clang-p2996-specific issues with their GCC equivalents**

The current file documents clang-p2996 issues (clang-scan-deps races, module cache corruption, std::mutex segfaults, etc.). Most of these are now stale.

Rewrite the file with these sections:

```markdown
# Compiler issues (GCC 16+)

## C-system headers must go through `storm_c_headers`

Including a C system header (`<sqlite3.h>`, `<libpq-fe.h>`, etc.) directly in
a `.cppm` translation unit that also does `import std;` produces redeclaration
errors for libc typedefs like `__mbstate_t`. The fix is to `import storm_c_headers;`
instead. See `src/storm_c_headers.cppm` for the symbol-export pattern.

If you need a new C function/type, add a `using ::name;` line to
`storm_c_headers.cppm` rather than `#include`-ing the header at the call site.

## `import std;` does not leak names into global namespace

Unlike clang's `-fbuiltin-module-map` header units, `import std;` keeps
`size_t`, `int64_t`, etc. strictly in `std::`. Use `using std::size_t;`
declarations after `import std;` if your module uses bare typedefs.

## Constexpr step limit

GCC uses `-fconstexpr-ops-limit=N` (operations, not steps). The `tests/` target
sets it to 134_217_728. Consteval JSON parsers can require very high limits.

## Module BMI rebuild required after source changes

GCC writes BMIs to `<build-dir>/gcm.cache/`. Editing a `.cppm` invalidates
downstream BMIs. CMake handles this automatically — but if you see
"compiled module file is too old" errors, delete the build dir and reconfigure.

## Known annotation gap

(Possibly not present in GCC 16.1.1 — verify before relying on cross-BMI
annotation reflection. See issue #226 follow-ups.)
```

- [ ] **Step 3: Commit**

```bash
git add docs/development/COMPILER_ISSUES.md
git commit -m "docs: rewrite COMPILER_ISSUES.md for GCC (#226)"
```

### Task 8.3: Update remaining docs

**Files:**
- Modify: `docs/development/GETTING_STARTED.md`
- Modify: `docs/development/FUZZING.md`
- Modify: `docs/development/SANITIZERS.md`
- Modify: `docs/development/PRE_COMMIT.md`
- Modify: `docs/development/CPP26_CODING_STANDARDS.md`
- Modify: `docs/README.md`
- Modify: `docs/architecture/DESIGN_DECISIONS.md`
- Modify: `docs/features/JOIN_OPERATIONS.md`

- [ ] **Step 1: Audit each file for clang-p2996 / LIBCXX_ROOT references**

```bash
for f in docs/development/GETTING_STARTED.md \
         docs/development/FUZZING.md \
         docs/development/SANITIZERS.md \
         docs/development/PRE_COMMIT.md \
         docs/development/CPP26_CODING_STANDARDS.md \
         docs/README.md \
         docs/architecture/DESIGN_DECISIONS.md \
         docs/features/JOIN_OPERATIONS.md; do
  echo "=== $f ==="
  grep -n "clang-p2996\|LIBCXX_ROOT\|libc++\|libcxx\|llvm-cov\|llvm-profdata\|clang-format\|clang-tidy" $f | head -10
done
```

- [ ] **Step 2: Edit each file**

For each file:
- Replace `clang-p2996` → `GCC 16+`
- Replace `LIBCXX_ROOT` mentions with the new equivalent (usually deletable)
- Replace setup instructions ("install Docker image / build clang from source") with "`sudo apt install gcc-16 g++-16`" (or equivalent for the doc's intended audience)
- For `FUZZING.md` and `SANITIZERS.md`: note GCC's MSAN limitation and libFuzzer-not-available status
- For `DESIGN_DECISIONS.md`: the "avoid std::function due to libc++ linker issues" justification may no longer apply under libstdc++. Test (Phase 4 should have surfaced any breakage) and either delete the warning or keep it with updated rationale
- For `JOIN_OPERATIONS.md`: same — the abstract base class workaround was a libc++-specific issue; verify and update

This task is text-only — each file is a 5-15 minute edit. Do them one at a time, commit per file.

- [ ] **Step 3: Verify clean**

```bash
grep -rn "clang-p2996\|LIBCXX_ROOT" docs/ | head -5
```

Expected: zero matches.

- [ ] **Step 4: Commit (one per file, or one bundle — pick what's reviewable)**

```bash
git add docs/
git commit -m "docs: update remaining docs for GCC-only toolchain (#226)"
```

### Task 8.4: Update `.claude/agents/*.md`

**Files:**
- Delete: `.claude/agents/clang.md` (entire file is clang-p2996-specific)
- Modify: every other `.claude/agents/*.md` that references clang/libc++

- [ ] **Step 1: Audit**

```bash
grep -rn "clang-p2996\|LIBCXX_ROOT\|libc++\|libcxx" .claude/agents/ | head -20
```

- [ ] **Step 2: Delete the clang-specific agent**

```bash
git rm .claude/agents/clang.md
```

- [ ] **Step 3: Edit other agent files**

For each agent file with references, same approach as Task 8.3: replace clang-p2996 mentions with GCC, delete obsolete sections about clang-specific workarounds.

- [ ] **Step 4: Commit**

```bash
git add .claude/agents/
git commit -m "docs: update Claude agent files for GCC-only toolchain (#226)"
```

### Task 8.5: Update memory entries

**Files:**
- `~/.claude/projects/-home-ihor-projects-storm-storm-develop/memory/MEMORY.md` and the per-entry files it indexes

- [ ] **Step 1: Audit memory for now-stale clang-p2996 facts**

```bash
grep -rln "clang-p2996\|LIBCXX_ROOT" ~/.claude/projects/-home-ihor-projects-storm-storm-develop/memory/
```

- [ ] **Step 2: For each stale entry, either update or delete**

Many of the memory entries documented clang-p2996 bugs (e.g., `feedback_cpp26_module_reflection_annotations.md`, `project_clang_tidy_silent_drift.md`). Mark them as historical/superseded by:
1. Updating the entry's `description` field to start with `[SUPERSEDED 2026-05-19 by #226 GCC migration]`
2. Updating the body to note "kept for history; no longer applies under GCC"

Or delete the entry entirely if it's pure clang-fork lore.

- [ ] **Step 3: Update `MEMORY.md` index lines if descriptions changed**

```bash
# manual edit
$EDITOR ~/.claude/projects/-home-ihor-projects-storm-storm-develop/memory/MEMORY.md
```

- [ ] **Step 4: No commit needed (these are user memory files, not repo files).**

---

## Phase 9: Final validation and PR

### Task 9.1: Full clean rebuild

- [ ] **Step 1: Wipe all build dirs**

```bash
rm -rf build/
```

- [ ] **Step 2: Configure and build Debug**

```bash
cmake --preset ninja-debug && cmake --build --preset ninja-debug
```

Expected: builds cleanly.

- [ ] **Step 3: Run full test suite**

```bash
ctest --preset ninja-debug
```

Expected: 100% passed.

- [ ] **Step 4: Configure and build Release + run benchmark**

```bash
cmake --preset ninja-release && cmake --build --preset ninja-release
ctest --preset ninja-release 2>&1 | tail -5
./build/release/benchmarks/storm_bench --benchmark_filter='Storm/SELECT/Single' --benchmark_repetitions=3 2>&1 | tail -20
```

Expected: tests pass; benchmark numbers within 10% of historical clang-p2996 baseline (96-108%). If significantly slower, note for the PR description and the risk register.

- [ ] **Step 5: Run pre-commit hook end-to-end**

```bash
echo "" >> docs/README.md
./commit.sh "test: trigger pre-commit hook"
git reset HEAD~1
git checkout docs/README.md
```

Expected: hook runs all stages (format, tidy, tests, coverage) and either commits or reports a specific stage that failed (so we can fix it).

### Task 9.2: Update Issue #226 checklist + open PR

- [ ] **Step 1: Check off completed DoD items**

```bash
gh issue view 226 --json body | python3 -c "
import sys, json
body = json.load(sys.stdin)['body']
print(body)
" > /tmp/issue-226.md
# Manual edit: replace '- [ ]' with '- [x]' for completed items
$EDITOR /tmp/issue-226.md
gh issue edit 226 --body-file /tmp/issue-226.md
```

- [ ] **Step 2: Push branch**

```bash
git push -u origin feature/226-gcc-build-support
```

- [ ] **Step 3: Open the PR**

```bash
gh pr create --base develop --title "Switch from clang-p2996 to GCC 16+ as primary compiler" --body "$(cat <<'EOF'
## Summary

Migrates Storm from the experimental `clang-p2996` fork + custom libc++ build to stock GCC 16 + libstdc++.

Closes #226.

## What changed

- Compiler gate hard-pinned to GCC ≥ 16
- `import <foo>;` → `import std;` across all 24 source + 45 test `.cppm`/`.cpp`/`.hpp` files
- New `src/storm_c_headers.cppm` isolates C-system headers from `import std;` redeclaration conflicts
- `using std::size_t;` (and related) injected per module
- `cmake/libcxx.cmake` deleted; `cmake/gcc_flags.cmake` replaces the GCC arm
- `cmake/coverage.cmake` rewritten for `gcov`+`lcov` (was `llvm-cov`+`llvm-profdata`)
- `cmake/format.cmake` and `scripts/run_clang_tidy.sh` use system `clang-format`/`clang-tidy`
- CI rebuilt around `apt install gcc-16` (no more `storm-ci` image)
- `docker-ci-image.yml` and `clang-tidy-sweep.yml` deleted (sweep can return as a follow-up)
- All docs + agent files updated

## Verification

- [x] `ctest --preset ninja-debug` passes (SQLite + PostgreSQL)
- [x] `cmake --build --preset ninja-release` succeeds; benchmarks run
- [x] `ninja-asan-ubsan` and `ninja-tsan` build + pass tests
- [ ] `ninja-msan` — disabled under GCC (needs MSAN-built libstdc++)
- [x] Coverage target (`coverage` / `coverage-html`) works under gcov/lcov

## Known follow-ups (separate issues)

- Verify annotation-across-BMI behaviour under GCC
- Rewrite `clang-tidy-sweep.yml` against system clang-tidy
- Investigate libFuzzer alternative for GCC fuzz targets
- Re-baseline benchmark numbers; update `benchmarks/README.md` if shifted

## Test plan

- [x] Local: `ctest --preset ninja-debug` green
- [x] Local: `ctest --preset ninja-release` green
- [ ] CI: all jobs green
- [ ] SonarCloud: clean gate

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

- [ ] **Step 4: Wait for CI**

```bash
# Wait 30 seconds for SonarCloud to spin up
sleep 30
/sonarcloud-status
gh pr checks --watch
```

- [ ] **Step 5: Address SonarCloud or CI issues until clean, then squash-merge**

Per project rules (`CLAUDE.md`): zero new SonarCloud issues, all CI green.

```bash
gh pr merge --squash
gh issue close 226
git checkout develop && git pull
```

---

## Self-Review Checklist

After writing this plan and before handoff, I verified:

1. **Spec coverage:** Every section of the updated Issue #226 body (Summary, Scope In/Out, Open Blockers 1–4, DoD checkboxes, Risk Register) maps to at least one task. Blocker #1 (C-headers) → Phase 3. Blocker #2 (bare typedefs) → Task 2.3. Blocker #3 (textual std-header includes) → Task 2.2. Blocker #4 (flag divergence) → Phase 1.

2. **Placeholder scan:** No "TBD" / "implement later" / "add appropriate error handling." Every code block is concrete. The one judgement call ("if Ubuntu 24.04 doesn't have GCC 16…") is explicitly flagged as a deployment question, not a placeholder.

3. **Type consistency:** The helper is `apply_cxx_flags` everywhere (not `apply_clang_flags`). The module is `storm_c_headers` everywhere (not `c_headers` or `storm_c`). The CMake property is `CXX_MODULE_STD` (not `CXX_STD_MODULE`).

4. **Risk surfaces flagged:** Annotation-across-BMI (Phase 8.5 reminder + risk register in DoD), MSAN limitation (Task 5.2), libFuzzer gap (Task 1.7), performance re-baselining (Task 9.1 Step 4).
