# Compiler Issues and Workarounds

Storm ORM uses experimental Clang with C++26 reflection. Here are known issues and workarounds.

## Compiler Requirements

- **Clang fork**: clang-p2996 with reflection support
- **Location**: `../clang-p2996/`
- **Custom libc++**: With reflection support
- **Module scanning**: clang-scan-deps
- **Flags**: `-freflection -fannotation-attributes`

## Known Issues

### 1. Module Cache Corruption

**Symptom**: `module '_Builtin_stdint' is defined in both [same_path] and [same_path]`

**Cause**: Module cache corruption with C++26 modules + GoogleTest + custom libc++

**Workaround**: Simply run the build command again - second attempt usually succeeds

```bash
ninja storm_tests  # May fail
ninja storm_tests  # Will succeed
```

**Nuclear option**:
```bash
rm -rf ~/.cache/clang/ModuleCache
ninja storm_tests
```

### 2. std::mutex in Modules

**Status (since #326 `import std;` migration)**: `std::mutex` /
`std::condition_variable` work in a module purview when supplied by `import std;`.
`src/db/pool.cppm` uses both directly and is validated under TSAN (1924/1924, zero
data races) and ASAN/UBSAN. The earlier textual `#include <mutex>` /
`#include <condition_variable>` in the global module fragment were dropped — the
types now come from the std module.

**Historical symptom** (pre-#326, header-unit world): the compiler could crash
when `std::mutex` was pulled in via a header-unit `import <mutex>;`. If you hit a
mutex-related compiler crash, prefer `import std;` over a header-unit import, and
keep mutex-using code in a module purview rather than a textual header included
after `import std;` (see Finding B in §9).

### 3. std::inplace_vector Not Available

**Symptom**: `header file <inplace_vector> cannot be imported`

**Workaround**: Use `std::array` with manual index tracking

**Example**:
```cpp
// Instead of: std::inplace_vector<T, N>
std::array<T, N> data;
size_t size = 0;
```

### 4. C Headers Cannot Be Imported

**Symptom**: `header file <cassert> cannot be imported because it is not known to be a header unit`

**Workaround**: Include C headers in module preamble (before `export module`)

**Example**:
```cpp
#include <cassert>    // Before export module
#include <cstring>

export module storm_orm_statements_base;
```

### 5. Most Vexing Parse with ConstexprString

**Symptom**: `type 'const std::array<char, N>' does not provide a call operator`

**Workaround**: Use braced initialization or direct member access

**Example**:
```cpp
// Wrong: Parentheses interpreted as function declaration
std::string str(array.data(), array.size());

// Right: Braced initialization
std::string str{array.data(), array.size()};

// Right: ConstexprString member access
std::string str{cs.data.data(), cs.len};
```

### 6. Missing Statement Methods

**Symptom**: `no member named 'column_count' in 'storm::db::sqlite::Statement'`

**Workaround**: Use raw SQLite handle via `stmt->handle()`

**Example**:
```cpp
// Instead of: stmt->column_count()
sqlite3_column_count(stmt->handle())
```

### 7. clang-p2996 libc++ Modules Layout Mismatch

**Symptom** (when enabling CMake's `import std;` support):
```
CMake Error: Cannot find source file:
  <LIBCXX_ROOT>/build/share/libc++/v1/std.compat.cppm
```

**Cause**: `<LIBCXX_ROOT>/build/lib/x86_64-…/libc++.modules.json` declares the
sources at `share/libc++/v1/std.cppm`, but the build places them under
`build/modules/c++/v1/`. Plain libc++ installs expose them via `share/`; the
clang-p2996 build directory does not.

**Workaround**: `cmake/libcxx.cmake` creates a symlink
`build/share/libc++/v1 -> build/modules/c++/v1` at configure time. The logic
is idempotent and won't overwrite a real directory if one exists.

**Related**: Issue [#326](https://github.com/spiritEcosse/storm/issues/326)
tracks the staged migration from per-header `import std.<sub>;` to a single
`import std;`.

### 8. CMake Picks libstdc++ Instead of libc++ for `import std`

**Symptom** (on any target with `CXX_MODULE_STD ON`):

```
[2/11] Scanning /usr/include/c++/16.1.1/bits/std.compat.cc for CXX dependencies
[3/11] Scanning /usr/include/c++/16.1.1/bits/std.cc for CXX dependencies
FAILED: CMakeFiles/__cmake_cxx_std_26.dir/usr/include/c++/16.1.1/bits/std.cc.o.ddi
…
/usr/include/c++/16.1.1/bits/std.cc:26:10: fatal error: 'bits/stdc++.h' file not found
```

**Cause**: CMake's `Clang-CXX-CXXImportStd.cmake` runs `clang++
-print-file-name=libstdc++.modules.json` when `CMAKE_CXX_STANDARD_LIBRARY`
isn't explicitly `libc++`. Clang resolves that against the host GCC's
`/usr/lib64/libstdc++.modules.json` and CMake then tries to compile GCC's
`bits/std.cc` with `-nostdinc++` and our libc++ include paths, which breaks
on `bits/stdc++.h`.

Pinning `CMAKE_CXX_STANDARD_LIBRARY` alone is not enough — the variable
isn't always consulted, and `CMAKE_CXX_STDLIB_MODULES_JSON` (CMake 4.2+) is
the direct override.

**Workaround**: top-level `CMakeLists.txt` sets
`CMAKE_CXX_STDLIB_MODULES_JSON` to clang-p2996's
`build/lib/x86_64-unknown-linux-gnu/libc++.modules.json` before `project()`,
guarded by `if(DEFINED LIBCXX_ROOT)`.

**Related**: Issue [#326](https://github.com/spiritEcosse/storm/issues/326).

### 9. `import std;` Migration — Findings & Header Rules

The tree consumes the C++26 named module via a single `import std;` (issues
[#326](https://github.com/spiritEcosse/storm/issues/326) /
[#332](https://github.com/spiritEcosse/storm/issues/332)) instead of per-header
`import <header>;` header units. Four findings govern how std headers interact
with the module.

**Finding A — `import std;` does NOT export `std::meta::`.**
libc++'s `std.cppm` `#include`s `<meta>` internally, but the reflection symbols
(`std::meta::info`, `std::meta::identifier_of`, the `^^` splice, …) are NOT
re-exported across the module boundary (clang-p2996 limitation, same family as
the annotations-lost-across-BMI issue). **Any TU using `std::meta::` MUST still
textually `#include <meta>`.** `import std;` alone is insufficient for reflection
code.

**Finding B — `#include <meta>` (and any textual std header) must come BEFORE the
imports in a non-module TU.** `<meta>` transitively pulls textual libc++ headers
(`<optional>` → `<compare>` → … → `promote.h`). If `import std;` (or `import
storm;`, which transitively imports std) is processed first, the later textual
headers redefine entities the std module already owns
(`error: redefinition of '__promote_t'`). In **module units** this is a non-issue
— the global module fragment is a separate context. In **textual headers /
plain TUs**, put textual std `#include`s before the imports, or drop them and let
`import std;` supply the types.

**Finding C — clang-tidy needs the module BMIs built first.** clang-tidy replays
`build/release/compile_commands.json` but does NOT reconstruct CMake's
`CXX_MODULE_STD` plumbing. An `import std;` TU fails with
`module 'std' not found` (and `@…modmap` not found) unless the release build has
already produced `std.pcm` + the per-TU `.modmap`. `commit.sh` builds the module
BMIs before clang-tidy, detected via the synthesized `__cmake_cxx_std_26` target
in `build/release/build.ninja` (NOT a `-fmodule-file=std=` flag — CMake's
import-std support wires the std module via ninja dyndep, which never appears in
`compile_commands.json`).

**Finding D — sanitizer/instrumentation flags must precede `add_library(storm)`.**
`cmake/sanitizers.cmake` applies `-fsanitize=…` globally via
`add_compile_options()` / `add_link_options()`, which only affect targets created
AFTER the include. The CMake-synthesized `__cmake_cxx_std_26` std-module target
compiles libc++'s `std.cppm`; if it misses the sanitizer flag, its
container-annotation calls (`__sanitizer_annotate_contiguous_container`)
link-fail with an undefined reference. `CMakeLists.txt` therefore includes the
sanitizer plumbing **before** `add_library(${PROJECT_NAME})`. Also: a build dir
configured before the import-std gate landed must be **wiped**, not
reconfigured — `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD` is a before-`project()` gate.

#### Which std `#include`s can be folded into `import std;`

| Header class | Rule |
|---|---|
| `<cassert>` | **KEEP** — `assert` is a macro; modules cannot export macros |
| `<meta>` | **KEEP** — `std::meta::` not re-exported (Finding A) |
| POSIX (`<csignal>`, `<sys/*.h>`, `<unistd.h>`, `<termios.h>`, `<poll.h>`) | **KEEP** — not part of `import std;` |
| third-party (`<sqlite3.h>`, `<plf_hive/…>`, `<libpq-fe.h>`, `<uuid.h>`) | **KEEP** — not std |
| pure-library (`<vector>`, `<string>`, `<format>`, `<utility>`, …) | **FOLD where context allows** (see below) |

Folding a pure-library header is only safe per location:

- **GMF of a `.cppm` that already has `import std;` in its purview** → redundant,
  drop it (e.g. `distinct.cppm` dropped `<utility>`; `pool.cppm` dropped
  `<mutex>` / `<condition_variable>`).
- **A `.cppm` GMF whose textual model header needs the type before the imports**
  → **KEEP** (e.g. `crud_benchmark.cppm` / `query_benchmark.cppm` keep `<tuple>`
  because `models.hpp`'s `Indexes<T>::type` is parsed in the GMF, before the
  purview's `import std;`; dropping it triggers the Finding B `__promote_t`
  redefinition).
- **A `.cppm` with NO `import std;`** (`parser`, `registry`, `wire`,
  `socket_server`, `tui` bench modules) → **KEEP textual**. Adding `import std;`
  would force `CXX_MODULE_STD ON` onto the `storm_tests` target (which compiles
  some of these as its own FILE_SET units); these modules deliberately stay
  header-free to avoid that.
- **A textual header `#include`d AFTER `import std;`** (dashboard
  `args/backup/db/events.hpp`, `fuzz/fuzz_models.h`) → **MUST drop** the std
  `#include`s (keeping them re-pulls libc++ headers after the module → Finding
  B). The single consumer (`main.cpp` / the fuzz harness) supplies them via
  `import std;`, with textual `<meta>` placed before its imports where the TU
  uses reflection. `fuzz_models.h` dropped its `<string>`; the harness TUs that
  use `std::meta::` keep textual `<meta>` before `import storm; import std;`.

### 10. `annotation_of_type` Segfaults on BMI-Imported Member Reflections

**Problem**: Calling `std::meta::annotation_of_type<X>(member)` on a reflection
that was **created in another module TU** (e.g. returned by an exported consteval
function and consumed across the BMI boundary) segfaults the compiler (exit 139,
crash in `ExprConstant.cpp` metafunction evaluation). In constraint-satisfaction
contexts the crash can instead surface as a misleading
`error: cannot take the reflection of an overload set`.

**Structural metafunctions are safe** on the same BMI-crossing reflection:
`is_nonstatic_data_member`, `parent_of`, `identifier_of`, `has_identifier` all
work. Only annotation lookup breaks (same family as the annotations-lost-across-BMI
issue, clang-p2996 [#262](https://github.com/bloomberg/clang-p2996/issues/262)).

**Solution**: Re-derive the member from `^^T` locally before reading annotations —
match by identifier, then call `annotation_of_type` on the freshly derived info:

```cpp
// ❌ Crashes when Member crossed a BMI boundary
auto attr = std::meta::annotation_of_type<meta::FieldAttr>(Member);

// ✅ Safe — annotation read from a locally derived reflection
for (auto m : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
    if (std::meta::identifier_of(m) == std::meta::identifier_of(Member)) {
        auto attr = std::meta::annotation_of_type<meta::FieldAttr>(m);
        // ...
    }
}
```

Found in #388: the `FKFieldOf<T, Member>` concept (base.cppm) constrains
`join<^^T::field>()` and must work with FK reflections produced by the benchmark
registry module (`storm_benchmark_registry::resolve_fk_field`).

## Debugging Tips

1. **Clean build**: `rm -rf build/ && cmake --preset ninja-debug`
2. **Clear module cache**: `rm -rf ~/.cache/clang/ModuleCache`
3. **Verbose output**: `cmake --build --preset ninja-debug -v`
4. **Module graph**: `clang-scan-deps --format=p1689 --`

## See Also

- [Module System](../architecture/MODULE_SYSTEM.md) - How modules are organized
- [Adding Features](ADDING_FEATURES.md) - Development workflow
