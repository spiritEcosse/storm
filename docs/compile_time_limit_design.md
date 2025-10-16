# Compile-Time LIMIT/OFFSET Duplicate Prevention Design

## Approach: Type-Based State Tracking

Use template parameters to encode whether `limit()` and `offset()` have been called, making duplicate calls a compile-time error.

### Implementation Strategy

```cpp
// Type tag to track state
template <bool HasLimit, bool HasOffset>
struct QuerySetState {};

template <class T,
          storm::db::DatabaseConnection ConnType = storm::db::sqlite::Connection,
          bool HasLimit = false,
          bool HasOffset = false>
class QuerySet {
    // ... existing members ...

    // limit() is only callable if HasLimit is false
    template <typename Self>
    constexpr auto limit(this Self&& self, size_t n)
        requires (!HasLimit)  // Compile-time check
    {
        // Return NEW type with HasLimit = true
        QuerySet<T, ConnType, true, HasOffset> result{std::forward<Self>(self)};
        result.limit_ = n;
        return result;
    }

    // offset() is only callable if HasOffset is false
    template <typename Self>
    constexpr auto offset(this Self&& self, size_t n)
        requires (!HasOffset)  // Compile-time check
    {
        // Return NEW type with HasOffset = true
        QuerySet<T, ConnType, HasLimit, true> result{std::forward<Self>(self)};
        result.offset_ = n;
        return result;
    }

    // Copy constructor for state transitions
    template <bool OtherLimit, bool OtherOffset>
    QuerySet(const QuerySet<T, ConnType, OtherLimit, OtherOffset>& other)
        : conn_(other.conn_)
        , limit_(other.limit_)
        , offset_(other.offset_)
        , join_stmt_(other.join_stmt_)
        // ... copy other state ...
    {}
};
```

## Usage Examples

### ✅ Valid Code (Compiles)
```cpp
auto qs = QuerySet<Person>{};

// Single limit - OK
auto r1 = qs.limit(10).select();

// Single offset - OK
auto r2 = qs.offset(5).select();

// Limit + offset - OK
auto r3 = qs.limit(10).offset(5).select();

// Offset + limit (reversed order) - OK
auto r4 = qs.offset(5).limit(10).select();
```

### ❌ Invalid Code (Compile Error)
```cpp
auto qs = QuerySet<Person>{};

// ERROR: Cannot call limit() twice
auto r1 = qs.limit(10).limit(20).select();
//                      ^^^^^^^
// Compiler error: no matching function for call to 'limit'
// note: candidate template ignored: constraints not satisfied [HasLimit = true]

// ERROR: Cannot call offset() twice
auto r2 = qs.offset(10).offset(20).select();
//                       ^^^^^^^^
// Compiler error: no matching function for call to 'offset'
```

## Pros and Cons Analysis

### ✅ Advantages

1. **Compile-Time Safety**
   - Catches errors at compile-time (best possible time)
   - Zero runtime overhead
   - Impossible to write incorrect code

2. **Self-Documenting API**
   - Type system enforces correct usage
   - IDE autocomplete won't suggest invalid operations
   - Compiler error messages guide users

3. **Modern C++ Best Practice**
   - Leverages type system for correctness
   - Follows "make illegal states unrepresentable" principle
   - Aligns with Storm ORM's compile-time philosophy

4. **Performance**
   - No runtime checks needed
   - Same zero-cost abstraction as current implementation
   - Types optimize away completely

### ❌ Disadvantages

1. **Implementation Complexity**
   - Requires template parameters for state tracking
   - Copy/move constructors need careful handling
   - Each state combination is a different type

2. **Template Bloat**
   - 2 boolean parameters = 4 type combinations
   - `QuerySet<Person, Conn, false, false>` vs `QuerySet<Person, Conn, true, false>`
   - More template instantiations (minimal impact with modern compilers)

3. **User Experience Concerns**
   - **Breaking Change**: Existing code may fail to compile
   - Compiler error messages might be cryptic for beginners
   - Stricter than industry-standard ORMs (SQLAlchemy, ActiveRecord allow duplicates)

4. **Edge Cases**
   ```cpp
   // User might want to "override" in some scenarios
   auto base_query = qs.limit(100);  // Default limit

   // Later, want to use different limit
   auto small_query = base_query.limit(10);  // ERROR! Can't do this
   ```

5. **Interaction with Other Features**
   - What about `join()`, `where()`, `order_by()` (future)?
   - Type combinatorics grow: `2^N` where N = number of state flags
   - Becomes unwieldy: `QuerySet<T, Conn, HasLimit, HasOffset, HasWhere, HasOrder>`

## Alternative: Middle-Ground Approach

### Option A: Compile-Time Warning (Better Errors)
```cpp
constexpr auto&& limit(this auto&& self, size_t n) {
    if constexpr (requires { self.limit_.has_value(); }) {
        if (self.limit_.has_value()) {
            // This branch is always taken if limit already set
            static_assert(false,
                "Multiple calls to limit() detected. Use only one limit() call per query chain.");
        }
    }
    self.limit_ = n;
    return self_cast(self);
}
```
**Issue**: Can't use `static_assert(false)` in this context reliably.

### Option B: Concept-Based Detection
```cpp
template <typename T>
concept HasLimitSet = requires(T t) {
    { t.limit_ } -> std::convertible_to<std::optional<size_t>>;
    requires t.limit_.has_value();  // Can't check value at compile-time
};
```
**Issue**: Can't check `std::optional` value at compile-time.

### Option C: Hybrid Approach (Runtime Debug, Release Accepts)
```cpp
constexpr auto&& limit(this auto&& self, size_t n) {
    #ifdef STORM_STRICT_QUERY_CHECKS
        if (self.limit_.has_value()) {
            throw std::logic_error("limit() called multiple times");
        }
    #endif
    self.limit_ = n;
    return self_cast(self);
}
```
**Benefit**: Opt-in strictness, doesn't break existing code.

## Recommendation

### For Storm ORM: **NO - Don't Add Compile-Time Check**

**Reasons:**

1. **Industry Standard Behavior**
   - All major ORMs (SQLAlchemy, ActiveRecord, Django) allow multiple calls
   - Users coming from other ORMs expect this behavior
   - "Last value wins" is intuitive and well-understood

2. **Diminishing Returns**
   - High implementation complexity
   - Marginal safety benefit (this is rarely a bug in practice)
   - Edge case: users might legitimately want to override

3. **Future Scalability**
   - Adding WHERE, ORDER BY, GROUP BY would explode type combinations
   - `QuerySet<T, Conn, Limit, Offset, Where, Order, Group, Having>` = 2^6 = 64 types!
   - Unmaintainable at scale

4. **Better Alternatives**
   - **Good documentation** (explain "last value wins")
   - **Debug assertions** (catch in testing, not production)
   - **Static analysis** (optional linter rule)

### Proposed Solution: Enhanced Debug Mode

```cpp
// In queryset.cppm
constexpr auto&& limit(this auto&& self, size_t n) {
    #ifndef NDEBUG
    // Debug mode: warn about potential misuse
    if (self.limit_.has_value()) {
        std::cerr << "Warning: limit() called multiple times. "
                  << "Previous value " << *self.limit_
                  << " will be overwritten with " << n << "\n";
    }
    #endif

    self.limit_ = n;
    return self_cast(self);
}
```

**Benefits:**
- ✅ Catches mistakes during development
- ✅ Zero runtime overhead in release builds
- ✅ Doesn't break existing code
- ✅ Simple implementation
- ✅ Matches industry standards

## When Compile-Time Checks ARE Worth It

Consider compile-time checks for:
1. **Mutually exclusive operations** (e.g., can't do both `inner_join()` and `left_join()`)
2. **Order-dependent operations** (e.g., must call `where()` before `limit()`)
3. **One-time operations** (e.g., can only call `execute()` once on a prepared statement)

For `limit()`/`offset()`, the "last value wins" behavior is:
- ✅ Standard across ORMs
- ✅ Intuitive
- ✅ Not error-prone in practice
- ✅ Useful for query composition

## Conclusion

**Skip the compile-time check for limit()/offset().**

Instead:
1. Add debug mode warnings (minimal overhead)
2. Document behavior clearly
3. Add test cases for multiple calls
4. Consider compile-time checks for truly ambiguous operations (future WHERE clauses, etc.)
