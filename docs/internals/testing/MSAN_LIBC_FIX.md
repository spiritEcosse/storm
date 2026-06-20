# MSAN + import std; + GTest false positive — libc++ fix in progress

## Status

**Partially applied, not yet working.** The fix is tracked in
`docs/development/fix_msan_libc_string.patch` (apply to `../clang-p2996/libcxx/include/string`).

The `ninja-msan` CI job is marked `continue-on-error: true` until this is resolved.

## Root cause

With `import std;` (#326), CMake compiles libc++'s `std.cppm` (the std named module)
with `-fsanitize=memory`. This makes `std::string`'s internal SSO layout fully
MSAN-instrumented. When GTest registers tests via static initializers (`TEST()` macro),
it constructs `std::string` objects whose SSO bits MSAN considers uninitialized —
because the std module boundary prevents MSAN's shadow from tracking those writes.

**Root cause: clang-p2996**, not GTest. GTest does correct C++ operations; the bug
is that clang-p2996's MSAN doesn't update shadow memory for writes that happen
through the std named module boundary.

## What the patch does

`fix_msan_libc_string.patch` contains several approaches tried in sequence:

1. **Extend `_LIBCPP_STRING_INTERNAL_MEMORY_ACCESS` to suppress MSAN** on `__is_long()`,
   `__get_long_pointer()`, `__get_short_pointer()`, `__get_pointer()`, `size()`, `length()`,
   `__get_long_size()`. These are the internal layout accessors that MSAN flags.

2. **Add `__msan_unpoison` calls** in constructors:
   - Copy constructor (`basic_string(const basic_string&)`): unpoison `__str.__rep_`
     before reading `__is_long()`.
   - Move constructor lambda: same.
   - `__annotate_new()`: unpoison `__rep_` + heap data after every construction.

3. **Global `extern "C" void __msan_unpoison`** declaration at file scope (needed
   because `__msan_unpoison` inside `std::__1` namespace would be mangled wrong).

## What still doesn't work

The MSAN chain keeps moving deeper — each suppressed read fixes one false positive
but another function becomes the trigger point. The chain at last attempt:

```
__is_long() → size() → empty() → FilePath::IsEmpty() → UnitTestImpl::AddTestInfo()
```

Even with `_LIBCPP_STRING_INTERNAL_MEMORY_ACCESS` on `size()`, the error still fires
because GTest creates `TestSuite` objects via `operator new` (which MSAN marks as
uninitialized) and the string copy inside `TestSuite`'s constructor goes through
suppressed inline paths that don't update MSAN's shadow.

## The real fix

The bug is in clang-p2996's MSAN: when a function is compiled into the std named
module (`std.cppm`), MSAN doesn't emit shadow-update instructions for writes to
SSO-layout fields. The fix belongs in compiler-rt's MSAN or in how clang compiles
module units with `-fsanitize=memory`. Upstream LLVM main may already have this fixed.

## How to continue

```bash
# Apply the incomplete patch
cd ../clang-p2996
git apply ../storm_develop/docs/development/fix_msan_libc_string.patch

# Rebuild libc++ headers (fast — no recompile)
ninja -C build/runtimes/runtimes-bins generate-cxx-headers

# Test
cd ../storm_develop
rm -rf build/msan
cmake --preset ninja-msan
ninja -C build/msan -j4 storm_tests
./build/msan/tests/storm_tests --gtest_list_tests
```

The remaining failure: `size()` at string:1301 → `__is_long()` → `string::empty()` →
`testing::internal::FilePath::IsEmpty()`. The `_LIBCPP_STRING_INTERNAL_MEMORY_ACCESS`
on `size()` is in the source but the MSAN error still fires — suggesting the suppression
is not propagating correctly to the GTest code path, or there's a different `size()`
code path being triggered.

## Alternative approaches not yet tried

1. Upgrade to a newer clang-p2996 that may have the MSAN shadow tracking fix.
2. Use MSAN's `-fsanitize-ignorelist` to suppress GTest paths at compile time.
3. Rebuild GTest with MSAN but use `__msan_allocated_memory()` after every
   `std::string` construction in GTest's test registration code.
