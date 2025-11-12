# Known Compiler Issues & Workarounds

Storm ORM uses an experimental Clang fork with C++26 reflection support (P2996). This document catalogs known issues and their workarounds.

## Compiler Environment

- **Compiler**: Experimental Clang with P2996 reflection support
- **Location**: `../clang-p2996/`
- **Custom libc++**: Reflection-enabled standard library
- **Module scanning**: `clang-scan-deps`
- **Reflection flags**: `-freflection -fannotation-attributes`

## 1. Module Cache Corruption

### Symptom
Build fails with error:
```
module '_Builtin_stdint' is defined in both [same_path] and [same_path]
```

### Cause
Module cache corruption in experimental P2996 compiler with C++26 modules + GoogleTest + custom libc++

### Workaround
**Simply run the build command again** - second attempt usually succeeds.

```bash
# If build fails with module cache error:
ninja storm_tests  # Will fail
ninja storm_tests  # Will succeed on second try
```

### Why It Works
First build attempt populates module cache, second build uses it correctly.

### When It Happens
Most commonly when building tests after clean or cache clear.

### Nuclear Option
If repeated attempts fail, clear cache completely:
```bash
rm -rf ~/.cache/clang/ModuleCache
ninja storm_tests  # Then retry
```

## 2. std::mutex Segfaults

### Symptom
Compiler crashes or segfaults when using `std::mutex` in C++26 modules.

### Cause
Known issue with `std::mutex` in experimental module implementation.

### Workaround
**Avoid `std::mutex` in module code**. Use external synchronization if thread safety is needed.

**Alternative**:
- Use per-thread connections (recommended for Storm ORM)
- Implement thread safety at application level
- Use atomic operations where possible

### Impact on Storm ORM
- Connection management layer is **NOT thread-safe**
- SQLite itself is thread-safe (`SQLITE_OPEN_FULLMUTEX`)
- Recommended approach: One connection per thread

## 3. std::inplace_vector Not Available

### Symptom
Build fails with error:
```
header file <inplace_vector> cannot be imported
```

### Cause
C++26 `std::inplace_vector` not yet implemented in custom libc++.

### Workaround
Use `std::array<T, N>` with manual index tracking instead.

**Before** (won't work):
```cpp
std::inplace_vector<FieldInfo, 50> fields;
fields.push_back(info);
```

**After** (works):
```cpp
std::array<FieldInfo, 50> fields;
size_t field_count = 0;
fields[field_count++] = info;
```

### Future
Will be available once custom libc++ is updated with `std::inplace_vector` implementation.

## 4. C Headers Cannot Be Imported as Modules

### Symptom
Build fails with error:
```
header file <cassert> (aka '...cassert') cannot be imported because it is not known to be a header unit
```

### Cause
C headers like `<cassert>`, `<cstring>`, etc. cannot be imported with `import` in modules.

### Workaround
**Include C headers in the module preamble** (before `export module`) using `#include`.

**Before** (won't work):
```cpp
export module storm_db_sqlite;
import <cassert>;  // ERROR!
```

**After** (works):
```cpp
#include <cassert>  // In preamble
export module storm_db_sqlite;
```

### Affected Headers
- `<cassert>`
- `<cstring>`
- `<cstdio>`
- `<cstdlib>`
- All other C compatibility headers

## 5. Most Vexing Parse with ConstexprString

### Symptom
Build fails with error:
```
type 'const std::array<char, N>' does not provide a call operator
```

### Cause
Parentheses initialization `std::string str(array.data(), array.size())` interpreted as function declaration.

### Workaround
Use **braced initialization** or direct member access.

**Before** (won't work):
```cpp
std::string str(array.data(), array.size());  // Parsed as function declaration!
```

**After** (works):
```cpp
std::string str{array.data(), array.size()};  // Braced initialization
```

### ConstexprString Specific
Access string data via `.data.data()` and `.len` members:
```cpp
ConstexprString<100> cs;
std::string str{cs.data.data(), cs.len};  // Correct
```

## 6. Missing Statement Methods

### Symptom
Build fails with error:
```
no member named 'column_count' in 'storm::db::sqlite::Statement'
```

### Cause
Custom Statement wrapper doesn't expose all SQLite functions.

### Workaround
Use **raw SQLite handle** via `stmt->handle()` and call SQLite C API directly.

**Before** (won't work):
```cpp
int col_count = stmt->column_count();  // Method doesn't exist
```

**After** (works):
```cpp
int col_count = sqlite3_column_count(stmt->handle());  // Direct C API
```

### Common Missing Methods
- `column_count()` → `sqlite3_column_count(handle())`
- `column_name()` → `sqlite3_column_name(handle(), index)`
- `column_type()` → `sqlite3_column_type(handle(), index)`

## 7. std::function Linker Errors

### Symptom
Linker errors when using `std::function` with custom libc++:
```
undefined reference to std::function related symbols
```

### Cause
Known issue with `std::function` in custom reflection-enabled libc++.

### Workaround
**Use abstract base classes for type erasure** instead of `std::function`.

**Before** (won't work):
```cpp
std::function<void(void*, void*)> extract_callback;
```

**After** (works):
```cpp
class ICallback {
    virtual void extract(void* stmt, void* obj) const = 0;
};

template <typename T>
class Callback : public ICallback {
    void extract(void* stmt, void* obj) const override {
        // Implementation
    }
};
```

### Impact on Storm ORM
This is why JOIN implementation uses `IJoinStatement` abstract base class instead of `std::function` callbacks.

## General Debugging Tips

### Enable Verbose Output
```bash
cmake --preset ninja-debug -DCMAKE_VERBOSE_MAKEFILE=ON
ninja -v storm_tests
```

### Check Module Dependencies
```bash
clang-scan-deps -format=p1689 -- \
  /home/ihor/projects/storm/clang-p2996/build/bin/clang++ \
  -std=c++26 -freflection -fannotation-attributes \
  src/storm.cppm
```

### Isolate Module Issues
Try compiling modules individually:
```bash
/home/ihor/projects/storm/clang-p2996/build/bin/clang++ \
  -std=c++26 -freflection -fannotation-attributes \
  -fsyntax-only src/orm/statements/select.cppm
```

### Clean Build
When in doubt, clean and rebuild:
```bash
rm -rf build/debug
cmake --preset ninja-debug -DENABLE_TESTS=ON
cmake --build --preset ninja-debug
```

## Reporting New Issues

If you encounter new compiler issues:

1. **Check if it's module-related**: Try removing module keywords
2. **Isolate the problem**: Create minimal reproducible example
3. **Document the workaround**: Update this file
4. **Report upstream**: If it's a compiler bug, report to clang-p2996 project

## Future Outlook

As C++26 reflection stabilizes and enters mainstream compilers:
- Module cache issues should be resolved
- `std::mutex` support in modules will be fixed
- Missing libc++ features will be implemented
- Custom libc++ workarounds will no longer be needed

Expected timeline: 2025-2026 for stable C++26 reflection in Clang/GCC.
