# Negative Value Safety in LIMIT/OFFSET API

## Type Safety with `size_t`

Storm ORM's `limit()` and `offset()` methods use `size_t` parameters, which provides **compile-time type safety** against negative values:

```cpp
// API signatures
constexpr auto&& limit(this auto&& self, size_t n);
constexpr auto&& offset(this auto&& self, size_t n);
```

## Compiler Protection

### 1. Unsigned Type

```cpp
static_assert(!std::numeric_limits<size_t>::is_signed,
              "size_t must be unsigned to prevent negative values");
```

`size_t` is an **unsigned integer type**, meaning it cannot represent negative values.

### 2. Compiler Warnings

Modern compilers (Clang, GCC with `-Wsign-conversion`) will warn when passing negative literals:

```cpp
QuerySet<Person> qs;

// ❌ Compiler warning: implicit conversion changes signedness: 'int' to 'size_t'
auto result = qs.limit(-1);

// ❌ Compiler warning: implicit conversion changes signedness
auto result = qs.offset(-100);
```

**Example warning (Clang):**
```
warning: implicit conversion changes signedness: 'int' to 'size_t' (aka 'unsigned long')
    auto result = qs.limit(-1);
                          ~~^~
```

### 3. Runtime Behavior (if forced)

If someone **explicitly casts** a negative value to `size_t`, it wraps around to a huge positive value:

```cpp
// Don't do this! Just documenting the behavior:
size_t wrapped = static_cast<size_t>(-1);
// wrapped == SIZE_MAX (18,446,744,073,709,551,615 on 64-bit systems)

size_t wrapped_neg100 = static_cast<size_t>(-100);
// wrapped_neg100 == SIZE_MAX - 99
```

**What happens in SQLite:**
- `LIMIT SIZE_MAX` → effectively unlimited (returns all rows)
- `OFFSET SIZE_MAX` → skips all rows (returns empty result)

This is **not recommended usage**, but the behavior is well-defined and won't cause crashes.

## Best Practices

### ✅ Correct Usage

```cpp
QuerySet<Person> qs;

// Use unsigned literals
auto result1 = qs.limit(10u).select();

// Or let compiler infer from size_t
size_t page_size = 20;
auto result2 = qs.limit(page_size).select();

// Natural numbers work fine
auto result3 = qs.limit(100).offset(50).select();
```

### ❌ Incorrect Usage (triggers warnings)

```cpp
int negative_limit = -1;
auto result = qs.limit(negative_limit);  // WARNING: sign conversion

// Passing literals
auto result2 = qs.offset(-50);  // WARNING: implicit conversion
```

### ⚠️ Conditional Logic

If you need to conditionally apply LIMIT/OFFSET based on signed inputs, validate first:

```cpp
int user_input_limit = /* from API request */;

if (user_input_limit > 0) {
    auto result = qs.limit(static_cast<size_t>(user_input_limit)).select();
} else {
    // No LIMIT - return all results
    auto result = qs.select();
}
```

## Why `size_t` Instead of `int`?

**Advantages:**

1. **Type safety**: Compiler catches negative values at compile-time
2. **Range**: Can represent larger positive values (up to SIZE_MAX)
3. **Idiomatic C++**: `size_t` is the standard type for sizes, counts, and indices
4. **SQLite compatibility**: SQLite internally uses 64-bit integers for LIMIT/OFFSET

**Design Decision:**

We prioritize **compile-time safety** over convenience. If you could pass `int`:
- `-1` might silently mean "no limit" (confusing convention)
- Negative values would require runtime validation
- API semantics become ambiguous

With `size_t`:
- Compiler enforces non-negative values
- Intent is crystal clear: "this is a size/count"
- No runtime checks needed

## Static Assertions

Storm ORM includes static assertions to verify type properties:

```cpp
// In implementation
static_assert(std::is_unsigned_v<size_t>,
              "LIMIT/OFFSET parameters must be unsigned");
```

## Comparison with Other ORMs

| ORM | LIMIT Parameter Type | Negative Value Handling |
|-----|---------------------|------------------------|
| **Storm ORM** | `size_t` (unsigned) | Compile-time error/warning |
| Django ORM | Python int (signed) | Runtime error if negative |
| SQLAlchemy | Python int (signed) | Passed to database (may error) |
| LINQ | `int` (signed) | Runtime `ArgumentOutOfRangeException` |
| ActiveRecord | Ruby int | Runtime error if negative |

Storm ORM is **stricter** than most ORMs, catching errors at compile-time instead of runtime.

## Summary

✅ Storm ORM's LIMIT/OFFSET API is **type-safe by design**
✅ Compiler warns about negative values at **compile-time**
✅ No runtime checks needed → **zero overhead**
✅ Follows C++ best practices (use `size_t` for sizes)
✅ Clear, unambiguous semantics

**Recommendation**: Enable compiler warnings (`-Wsign-conversion`) to catch potential issues early!
