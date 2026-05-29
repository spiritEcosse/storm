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

### 2. std::mutex Segfaults

**Symptom**: Compiler crashes when using `std::mutex` in modules

**Workaround**: Avoid mutex in module code, use external synchronization

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

**Symptom** (when `ENABLE_IMPORT_STD_PROBE=ON` or any target with
`CXX_MODULE_STD ON`):

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

## Debugging Tips

1. **Clean build**: `rm -rf build/ && cmake --preset ninja-debug`
2. **Clear module cache**: `rm -rf ~/.cache/clang/ModuleCache`
3. **Verbose output**: `cmake --build --preset ninja-debug -v`
4. **Module graph**: `clang-scan-deps --format=p1689 --`

## See Also

- [Module System](../architecture/MODULE_SYSTEM.md) - How modules are organized
- [Adding Features](ADDING_FEATURES.md) - Development workflow
