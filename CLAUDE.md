# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**📚 Full Documentation**: See [docs/README.md](docs/README.md) for comprehensive feature documentation, architecture guides, and development workflows.

## Project Overview

Storm is a modern C++26 ORM library for SQLite using cutting-edge C++26 reflection to automatically map C++ structs to database tables without macros.

**Performance Summary:**

Storm ORM achieves **96-108% efficiency** vs raw SQLite across all operations (Release builds, fair comparison):

- **INSERT**: ~97% efficiency for single, 96-108% for batch operations
- **SELECT**: ~96% efficiency (raw pointer caching, expression address tracking)
- **UPDATE**: ~93-94% efficiency (optimized with flat code, cached statements)
- **DELETE**: Fast single and batch deletions
- **JOIN**: Type-erased abstract base class pattern
- **DISTINCT**: Pure C++26 reflection-based implementation
- **WHERE**: Comprehensive operator support with compile-time dispatch

📊 **Detailed benchmark results**: See [benchmarks/README.md](benchmarks/README.md) for complete performance data, methodology, and analysis.

**Key Innovations**: Compile-time SQL generation, 3-level statement caching, thread-local SQL caching, optimized row extraction, fully inlined field binding, abstract base class pattern for type-erased JOIN operations, pure C++26 reflection for WHERE clauses.

## Critical Safety Rules

**⚠️ IMPORTANT: These rules must NEVER be violated:**

1. **NEVER Delete .git Repository**
   - Do not run `rm -rf .git` or any command that deletes the `.git` directory
   - The `.git` directory contains all project history and must be preserved

2. **NEVER Push Without User Approval**
   - Do not run `git push` unless explicitly requested by the user
   - Always ask for permission before pushing to remote repository
   - When committing changes, wait for user confirmation before pushing
   - Exception: If user explicitly says "commit and push", then both operations are approved

3. **MANDATORY: Benchmark After ANY Code Changes**
   - **After suggesting/implementing ANY improvement, IMMEDIATELY run benchmarks**
   - This applies to ALL changes, even "zero overhead" or "refactoring only" changes
   - **If benchmarks show ANY slowdown (even 1-2%), REVERT IMMEDIATELY**
   - Try alternative approach if available, or keep original code
   - **Never declare success without benchmark confirmation**
   - Remember: Performance > Code Cleanliness for ORMs

   **Why This Matters:**
   - Binary layout changes affect instruction cache unpredictably
   - Even removing dead code can change memory layout
   - Template/lambda changes can affect inlining decisions
   - "Unrelated" code can regress due to code placement
   - Benchmarks are the only source of truth

   **Mandatory Workflow:**
   ```bash
   # 1. Implement change
   # 2. Build RELEASE (REQUIRED - debug builds are not representative)
   cmake --preset ninja-release -DENABLE_BENCH=ON
   cmake --build --preset ninja-release

   # 3. RUN BENCHMARKS (for affected code paths)
   ./build/release/benchmarks/storm_bench --filter=<test_name> --iterations=10000
   ./build/release/benchmarks/storm_bench --filter=insert_batch --scale-test  # Test all batch sizes

   # 4. Compare with baseline - if ANY regression:
   git stash  # or git checkout -- <files>

   # 5. Only after confirming zero regression:
   # Proceed with commit
   ```

   **CRITICAL: ALWAYS use Release builds for benchmarks**
   - Debug builds have 10-100x performance degradation
   - Debug builds cannot detect micro-optimizations
   - Benchmark results are meaningless without `-O3` optimization

4. **MANDATORY: Update Documentation After Changes**
   - **After ANY significant change, IMMEDIATELY update affected README/documentation files**
   - Documentation must always reflect the current state of the codebase
   - Out-of-date documentation is worse than no documentation

   **When to Update Documentation:**

   **Must Update:**
   - ✅ **Project structure changes**: Add/remove/rename directories or major files
   - ✅ **Module architecture changes**: New modules, module reorganization, import changes
   - ✅ **API changes**: New public functions, changed signatures, removed features
   - ✅ **Build system changes**: CMake targets, presets, compilation flags, dependencies
   - ✅ **Performance characteristics**: New benchmarks, significant speedups/regressions
   - ✅ **Usage patterns**: New ways to use features, changed workflows
   - ✅ **Feature additions**: New ORM operations, query capabilities, supported types
   - ✅ **Feature removals**: Deprecated/removed functionality
   - ✅ **Configuration changes**: New environment variables, config files, command-line args
   - ✅ **Compiler requirements**: Clang version updates, new C++ features used
   - ✅ **Known issues/workarounds**: New compiler bugs, platform-specific issues

   **Watch in Documentation:**
   - 📝 **File/directory paths**: Ensure all referenced paths exist and are correct
   - 📝 **Command examples**: Test that commands actually work as shown
   - 📝 **Code snippets**: Verify code compiles and demonstrates current API
   - 📝 **Build instructions**: Confirm CMake presets, targets, and flags are accurate
   - 📝 **Feature lists**: Remove deprecated features, add new capabilities
   - 📝 **Performance numbers**: Update benchmark results when implementation changes
   - 📝 **Architecture diagrams**: Keep module structure, inheritance hierarchy current
   - 📝 **Supported types**: Update when adding/removing field type support
   - 📝 **Return types**: std::expected, std::optional usage changes
   - 📝 **Links**: Ensure cross-references between docs point to existing files
   - 📝 **Version requirements**: Compiler versions, library dependencies
   - 📝 **Migration guides**: Update when breaking API changes occur

   **Documentation Files to Check:**
   ```bash
   # Primary documentation
   CLAUDE.md                              # Project overview, rules, quick start
   README.md                              # User-facing main documentation

   # Feature-specific READMEs
   benchmarks/README.md                   # Unified benchmark system documentation
   docs/README.md                         # Documentation index

   # Architecture documentation
   docs/architecture/design-decisions.md  # Design rationale
   docs/architecture/module-structure.md  # Module organization

   # Development guides
   docs/development/getting-started.md    # Setup instructions
   docs/development/common-tasks.md       # How-to guides
   docs/development/performance-guidelines.md  # Performance rules

   # Reference documentation
   docs/reference/field-types.md          # Supported types mapping
   docs/reference/statement-caching.md    # Caching architecture
   docs/reference/compiler-issues.md      # Known compiler bugs

   # Benchmark results
   docs/benchmarks/results.md             # Performance measurements
   docs/benchmarks/join-analysis.md       # JOIN performance details
   docs/benchmarks/distinct-analysis.md   # DISTINCT performance details
   ```

   **Mandatory Workflow:**
   ```bash
   # 1. Make code changes
   # 2. Identify affected documentation
   # 3. Update ALL affected .md files
   # 4. Verify examples still work:

   # Test code snippets
   grep -r "```cpp" docs/ | # Extract and test compile

   # Test commands
   grep -r "```bash" docs/ | # Verify commands execute

   # Check paths
   grep -r "src/" docs/ | # Ensure referenced files exist

   # 5. Commit code AND documentation together
   git add src/ docs/ CLAUDE.md README.md
   git commit -m "feat: add feature X

   - Implementation details
   - Update docs/feature/X.md with usage
   - Update CLAUDE.md with performance notes"
   ```

   **Examples:**

   **Example 1: Adding new ORM operation**
   - Update `docs/development/common-tasks.md` with how to use it
   - Update `CLAUDE.md` performance summary if benchmarked
   - Update `README.md` feature list
   - Update `docs/architecture/module-structure.md` if new module

   **Example 2: Changing benchmark system**
   - Update `benchmarks/README.md` with new usage
   - Update `CLAUDE.md` benchmark workflow if commands change

   **Example 3: Refactoring module structure**
   - Update `docs/architecture/module-structure.md` with new layout
   - Update `CLAUDE.md` "Module Structure" section
   - Update all file path references in documentation
   - Update `docs/development/common-tasks.md` if import paths change

   **Example 4: New compiler requirement**
   - Update `CLAUDE.md` prerequisites
   - Update `docs/development/getting-started.md` setup instructions
   - Update `docs/reference/compiler-issues.md` if new workarounds needed

   **Verification Checklist Before Commit:**
   - [ ] All referenced file paths exist
   - [ ] All command examples execute successfully
   - [ ] All code snippets compile
   - [ ] Performance numbers match latest benchmarks
   - [ ] Cross-references link to existing documentation
   - [ ] Feature lists match actual capabilities
   - [ ] No outdated information remains

5. **MANDATORY: Use quick_commit.sh for All Commits**
   - **ALWAYS use `./quick_commit.sh "commit message"` instead of manual git commands**
   - Do NOT use `git add`, `git commit` manually - use the script
   - The script automatically:
     - Runs clang-format on all source files
     - Runs unit tests (fails if any test fails)
     - Creates commit with proper formatting
     - Pushes to remote

   **Error Handling:**
   - If quick_commit.sh fails, try to fix the issue and retry
   - Common fixes: simplify commit message (avoid special characters), fix failing tests
   - If unable to fix, inform the user about the specific error
   - NEVER fall back to manual git commands - always use quick_commit.sh

   ```bash
   # Correct way to commit:
   ./quick_commit.sh "fix: resolve SonarCloud issues"

   # WRONG - do not use manual git commands:
   # git add . && git commit -m "message" && git push
   ```

   **Why This Matters:**
   - Ensures code formatting is always consistent
   - Prevents commits with failing tests
   - Standardizes commit workflow across the project

6. **MANDATORY: Show Files and Get Approval Before Committing**
   - **Before running `quick_commit.sh`, ALWAYS show the user what will be committed**
   - The script runs in non-interactive mode when executed by Claude, bypassing its built-in approval
   - User must explicitly approve the file list before commit proceeds

   **Required Workflow:**
   ```bash
   # Step 1: Show files that will be committed
   git status --short

   # Step 2: Ask user: "Proceed with commit?"

   # Step 3: Only after user says "yes":
   ./quick_commit.sh "commit message"
   ```

   **Why This Matters:**
   - Prevents accidental commits of unintended files
   - User maintains control over what gets committed
   - Script's interactive approval is bypassed in automation

## Quick Start

### Build & Test

```bash
# Debug build with tests
cmake --preset ninja-debug -DENABLE_TESTS=ON
cmake --build --preset ninja-debug
ctest --test-dir build/debug --output-on-failure

# Release build
cmake --preset ninja-release
cmake --build --preset ninja-release
```

### Quick Commit Workflow

```bash
# Usage: ./quick_commit.sh [commit message]

# With custom commit message
./quick_commit.sh "fix: resolve ODR violation in Message struct"

# Without message (uses auto-generated default: "chore: - run code formatting")
./quick_commit.sh
```

This script:
1. Runs clang-format on all source files
2. Runs unit tests (fails if any test fails)
3. Uses provided commit message or auto-generates one
4. Commits changes and pushes to remote

### Benchmarking

**⚠️ CRITICAL: ONLY use Release builds for benchmarks!**

```bash
# Build release first (MANDATORY)
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release

# Quick validation during development (~3-5 min)
./build/release/benchmarks/storm_bench --quick                   # All tests, 0.3x iterations
./build/release/benchmarks/storm_bench --quick -c SELECT         # Quick SELECT tests only

# Thorough regression testing before commits (~15-20 min)
./build/release/benchmarks/storm_bench --thorough                # All tests, 1.5x iterations

# Standard benchmarks (uses JSON-defined iterations, ~10 min)
./build/release/benchmarks/storm_bench                           # All tests
./build/release/benchmarks/storm_bench --list                    # List available tests
./build/release/benchmarks/storm_bench -c SELECT                 # Run SELECT* categories (prefix match)
./build/release/benchmarks/storm_bench -c SELECT --list          # Preview which tests will run
./build/release/benchmarks/storm_bench --filter=insert_batch_100 # Run specific test (exact match)
./build/release/benchmarks/storm_bench --filter=insert_batch --scale-test  # Test performance degradation
./build/release/benchmarks/storm_bench --iterations=10000        # Override iterations for all tests

# See benchmarks/README.md for detailed guide
```

**Benchmark Modes:**
| Mode | Flag | Runtime | Use Case |
|------|------|---------|----------|
| Default | (none) | ~10 min | Standard benchmarking |
| Quick | `--quick` | ~3-5 min | Development validation |
| Thorough | `--thorough` | ~15-20 min | Pre-commit regression testing |

**Why Release-Only:**
- Debug builds: 10-100x slower (meaningless results)
- No `-O3` optimization = no inlining, no loop unrolling
- Cannot detect real-world performance characteristics

### Prerequisites
- Custom Clang with C++26 reflection (`../clang-p2996/`)
- SQLite3 development libraries
- CMake 3.30+, Ninja

See [Getting Started Guide](docs/development/getting-started.md) for detailed setup.

## Architecture Overview

### Module Structure

```
src/
├── storm.cppm                  # Main module
├── db/
│   ├── concept.cppm            # Database concepts
│   └── sqlite.cppm             # SQLite implementation
└── orm/
    ├── queryset.cppm           # QuerySet ORM interface
    ├── utilities.cppm          # ConstexprString, SQLCache
    └── statements/             # INSERT, SELECT, UPDATE, DELETE, DISTINCT, JOIN
```

See [Architecture Documentation](docs/architecture/) for detailed design.

### Key Design Decisions

1. **C++26 Reflection-Based ORM** - Automatic field mapping using `std::meta`
2. **Concept-Based Abstraction** - PostgreSQL/MySQL support without ORM changes
3. **Compile-Time SQL Generation** - Zero runtime overhead with ConstexprString
4. **Statement-Level Caching** - 20x+ speedup for repeated operations
5. **Thread-Local SQL Caching** - 94% improvement for bulk operations
6. **Index Sequence Optimization** - Fold expressions replace recursive templates
7. **Batch Operations** - Smart thresholds (SQLite limit = 999 variables)
8. **JOIN Architecture** - Type-erased SQL builder without std::function
9. **Auto-Generated IDs** - Returns IDs from INSERT operations
10. **DISTINCT Support** - Single and multi-field with type safety

See [Design Decisions](docs/architecture/design-decisions.md) for detailed explanations.

### Why Benchmark Headers Remain as `.hpp`

The `benchmarks/` directory uses traditional `.hpp` headers instead of C++26 modules. This is **intentional and required**:

1. **Macros cannot be exported from modules** - `timing.hpp` and `timing_trace.hpp` define `STORM_TRACE`, `TRACE_INIT`, etc.
2. **`#embed` requires headers** - `parser.hpp` uses `#embed "tests/benchmark_tests.json"` for compile-time JSON parsing
3. **Dependency chain** - `runner.hpp` includes `parser.hpp`, making the entire benchmark system header-based

**Benchmark test definitions workflow:**
- `benchmarks/tests/benchmark_tests.yaml` — Human-friendly source of truth (edit this)
- `benchmarks/tests/benchmark_tests.json` — Auto-generated at build time (do not edit)
- `benchmarks/scripts/yaml_to_json.py` — Converter script (runs automatically via CMake)

**Architecture split:**
- `src/` — C++26 modules (`.cppm`) for the ORM library
- `benchmarks/` — Headers (`.hpp`) for testing infrastructure

This separation is correct. Do not attempt to convert benchmark headers to modules.

## Performance Guidelines

**Performance testing is mandatory** for all new features. Target: ≥95% of raw SQLite efficiency.

### Workflow

```bash
# 1. Implement feature
# 2. Create benchmark: benchmarks/bench_<feature>.cpp
# 3. Run benchmarks
cmake --preset ninja-release -DENABLE_BENCH=ON
cmake --build --preset ninja-release
./build/release/benchmarks/bench_<feature> --size=10000

# 4. Compare with raw SQLite
# If Storm: 8.5M/sec, Raw: 10M/sec → 85% efficiency ✅ GOOD
# If Storm: 5M/sec, Raw: 10M/sec → 50% efficiency ❌ NEEDS WORK

# 5. Document results in docs/benchmarks/results.md
# 6. Commit with performance metrics
git commit -m "feat: add FEATURE (85% of raw SQLite)"
```

### Design Principles Balance

- **DRY/KISS** principles apply, but **performance takes precedence**
- If abstraction costs >10% performance → duplicate code
- Complex optimizations justified if >20% performance gain
- Always profile before optimizing

### Flat Code vs Nested Lambdas

**For hot paths, prefer flat code over nested lambdas.** Benchmarks show ~3-4% improvement.

```cpp
// ❌ SLOW: Nested lambdas (90% efficiency)
execute_with_transaction(conn, true,
    [this, objects]() {                          // Lambda 1: captures
        return execute_with_statement(conn, sql,
            [this, objects](auto& stmt) {        // Lambda 2: captures again
                for (...) { ... }
            });
    });

// ✅ FAST: Flat code (93-94% efficiency)
if (!cached_stmt_) { cached_stmt_ = conn_->prepare_cached(sql); }
conn_->execute("BEGIN TRANSACTION");
for (const auto& obj : objects) {
    cached_stmt_->reset();
    bind(...);
    cached_stmt_->execute();
}
conn_->execute("COMMIT");
```

**Why lambdas are slower:**
- Capture storage overhead (storing `this`, spans, etc.)
- Indirect call through function pointer
- Compiler inlining barriers at lambda boundaries
- Extra stack frame creation per lambda

**When to use each:**
| Flat Code | Lambdas |
|-----------|---------|
| Hot paths (millions of calls) | Cold paths (setup, config) |
| Inner loops, batch operations | Callbacks, event handlers |
| Performance-critical ORM ops | Code reuse across callers |

### Statement Pointer Caching (MANDATORY for Hot Paths)

**For single-row operations called in loops, cache the statement pointer locally.** Benchmarks show ~23% improvement.

```cpp
// ❌ SLOW: Hash lookup every call (78% efficiency)
auto execute_one(const T& obj) {
    auto stmt = conn_->prepare_cached(sql);  // Hash lookup EVERY call
    stmt->reset();
    bind(...);
    stmt->execute();
}

// ✅ FAST: Cache pointer, skip hash lookup (96% efficiency)
auto execute_one(const T& obj) {
    if (!cached_stmt_) {                              // First call only
        cached_stmt_ = conn_->prepare_cached(sql);   // Hash lookup once
    }
    cached_stmt_->reset();                           // Direct pointer access
    bind(...);
    cached_stmt_->execute();
}
```

**Why this matters:**
- `prepare_cached()` does hash lookup using SQL string as key
- String hashing + hash table lookup costs ~20-30 cycles per call
- For 10,000 single deletes: 200,000-300,000 wasted cycles

**Benchmark evidence (single DELETE):**
| Pattern | Ops/sec | Efficiency |
|---------|---------|------------|
| Without pointer cache | 5.02 M | 78.2% |
| With pointer cache | 6.18 M | 96.3% |

**When to use:**
- Single-row operations (`execute_one`, `remove_one`, etc.)
- Any method called repeatedly in a loop
- Hot paths with millions of potential calls

**When NOT needed:**
- Batch operations (hash lookup amortized over many rows)
- Cold paths (setup, initialization)
- Operations called once per request

### Raw Pointer Caching in Hot Loops (5-6% improvement)

**For query loops extracting many rows, cache the raw `sqlite3_stmt*` pointer.** Benchmarks show ~5-6% improvement.

```cpp
// ❌ SLOW: unique_ptr::get() called on every column (90.6% efficiency)
while (stmt->step() == SQLITE_ROW) {
    obj.id = sqlite3_column_int64(stmt->handle(), 0);    // handle() = unique_ptr::get()
    obj.name = sqlite3_column_text(stmt->handle(), 1);   // handle() again
    obj.age = sqlite3_column_int(stmt->handle(), 2);     // handle() again
    // ... 6+ calls per row × millions of rows
}

// ✅ FAST: Cache raw pointer once (96% efficiency)
sqlite3_stmt* raw_stmt = stmt->handle();  // Cache ONCE before loop
while (sqlite3_step(raw_stmt) == SQLITE_ROW) {
    obj.id = sqlite3_column_int64(raw_stmt, 0);    // Direct pointer
    obj.name = sqlite3_column_text(raw_stmt, 1);   // No indirection
    obj.age = sqlite3_column_int(raw_stmt, 2);     // Maximum speed
}
```

**Why this matters:**
- `unique_ptr::get()` is not free - it's a function call with pointer dereference
- Called 6+ times per row (once per column)
- For 10,000 rows: 60,000+ unnecessary function calls
- Compiler may not inline across translation units

**Benchmark evidence (SELECT WHERE with 10K rows):**
| Pattern | Efficiency |
|---------|------------|
| Without raw pointer cache | 90.6% |
| With raw pointer cache | 96% |

### Expression Address Caching (Skip SQL Building)

**For repeated queries with same WHERE expression, track expression address to skip SQL string building.**

```cpp
// Inside statement class
mutable const void* cached_expr_addr_ = nullptr;
mutable Statement* cached_stmt_ = nullptr;

auto execute(const WhereExpr& expr) {
    const void* expr_addr = static_cast<const void*>(expr.get());

    // Cache hit: same expression object, skip SQL building
    if (expr_addr == cached_expr_addr_ && cached_stmt_) {
        cached_stmt_->reset();
        bind_params(cached_stmt_, expr);
        return execute_loop(cached_stmt_);
    }

    // Cache miss: build SQL, prepare statement
    std::string sql = build_sql(expr);
    cached_stmt_ = conn_->prepare_cached(sql);
    cached_expr_addr_ = expr_addr;
    bind_params(cached_stmt_, expr);
    return execute_loop(cached_stmt_);
}
```

**Critical: Invalidate cache on reset() to prevent ABA problem:**
```cpp
void invalidate_cache() {
    cached_expr_addr_ = nullptr;
    // Note: don't clear cached_stmt_ - it's still valid in connection cache
}

// In QuerySet::reset()
void reset() {
    where_expr_.reset();
    select_stmt_->invalidate_cache();  // CRITICAL: prevent stale pointer match
}
```

**ABA Problem**: New expression allocated at same address as freed expression → stale cache hit → wrong SQL used.

## Database-Agnostic Module Pattern

**MANDATORY: Use templates for cross-module inlining in hot paths.**

C++26 modules prevent function inlining across module boundaries because function bodies aren't visible to importers. The **template trick** solves this:

```cpp
// ❌ SLOW: Non-template method - cannot be inlined across modules
class Statement {
    auto step_raw() noexcept -> int {
        return sqlite3_step(raw_);  // Body not visible to importers
    }
};

// ✅ FAST: Template method - body visible, enables inlining
class Statement {
    template <typename = void>
    [[nodiscard]] __attribute__((always_inline)) auto step_raw() noexcept -> int {
        return sqlite3_step(raw_);  // Body available for inlining!
    }
};
```

### Why This Works

Templates must have visible definitions for instantiation, so the compiler includes template bodies in the module interface. This enables `always_inline` to work across modules.

### Rules for Database-Agnostic Modules

| DO | DON'T |
|----|-------|
| Make hot-path methods `template <typename = void>` | Include DB headers in ORM modules |
| Use `__attribute__((always_inline))` with templates | Use database-specific types in ORM interfaces |
| Cache raw pointers in Statement class | Rely on LTO for inlining |
| Define constants (`ROW_AVAILABLE`) in Statement | Make cold-path methods templates |

### Example: Database-Agnostic SELECT

```cpp
// src/orm/statements/select.cppm
module;
#include <meta>  // C++26 reflection
// NOTE: No #include <sqlite3.h> - fully database-agnostic!

export module storm_orm_statements_select;
import storm_db_sqlite;  // Backend with template methods

// Uses Statement methods - works with SQLite, PostgreSQL, etc.
while (stmt->step_raw() == Statement::ROW_AVAILABLE) {
    obj.id = stmt->extract_int64(0);
    obj.name = stmt->extract_text_view(1);
}
```

### Performance Results

| Approach | Efficiency |
|----------|------------|
| Direct `sqlite3_*` calls | 96-97% |
| Non-template Statement methods | 92-94% |
| **Template Statement methods** | **95-96%** |

The ~1% overhead is acceptable for full database abstraction.

**Full documentation**: See [docs/architecture/MODULE_SYSTEM.md](docs/architecture/MODULE_SYSTEM.md) for complete implementation guide.

## Writing Fair Benchmarks

**⚠️ CRITICAL: Unfair benchmarks lead to wrong optimization decisions.**

### 1. Setup Outside Loop, Execute Inside

```cpp
// ❌ UNFAIR: Storm does setup inside, raw SQLite does setup outside
int storm_benchmark(int iterations) {
    for (int i = 0; i < iterations; i++) {
        qs.where(age > 30).select();  // WHERE built every iteration
    }
}

int raw_benchmark(int iterations) {
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);  // Prepared ONCE
    for (int i = 0; i < iterations; i++) {
        sqlite3_reset(stmt);
        sqlite3_step(stmt);
    }
}

// ✅ FAIR: Both do setup once, execute in loop
int storm_benchmark(int iterations) {
    auto where_clause = build_where();
    qs.where(where_clause);           // Set WHERE once
    for (int i = 0; i < iterations; i++) {
        qs.select();                  // Only execute in loop
    }
    qs.reset();                       // Cleanup after
}

int raw_benchmark(int iterations) {
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, value); // Bind once
    for (int i = 0; i < iterations; i++) {
        sqlite3_reset(stmt);          // Reset required for re-execution
        while (sqlite3_step(stmt) == SQLITE_ROW) { ... }
    }
}
```

### 2. Same Algorithm for Both

```cpp
// ❌ UNFAIR: Storm uses chunked bulk SQL, raw uses single inserts
// Storm: INSERT INTO t VALUES (?,?),(?,?),(?,?)  -- 3 rows at once
// Raw:   INSERT INTO t VALUES (?,?) × 3          -- 3 separate statements

// ✅ FAIR: Both use same strategy
// Storm: INSERT INTO t VALUES (?,?),(?,?),(?,?)
// Raw:   INSERT INTO t VALUES (?,?),(?,?),(?,?)
```

### 3. Same Container Types

```cpp
// ❌ UNFAIR: Storm uses plf::hive, raw uses std::vector
plf::hive<Model> storm_results;  // O(1) insert, stable iterators
std::vector<Model> raw_results;  // O(1) amortized, may reallocate

// ✅ FAIR: Both use same container
plf::hive<Model> storm_results;
plf::hive<Model> raw_results;
```

### 4. Measure Same Work

```cpp
// ❌ MISLEADING: Comparing throughput with different result sizes
// DISTINCT + WHERE: 78 rows → 0.13M rows/sec (looks slow)
// DISTINCT + JOIN:  10K rows → 8.75M rows/sec (looks fast)

// ✅ CORRECT: Use latency (ms/query) for queries with different result sizes
// DISTINCT + WHERE: 0.588ms/query (actually FASTEST)
// DISTINCT + JOIN:  1.143ms/query (actually slower)
```

### 5. Runtime vs Compile-Time Fairness

```cpp
// ❌ UNFAIR: Storm uses runtime batch_size, raw uses compile-time
template <int BatchSize>  // Raw gets free compile-time optimization
void raw_benchmark() {
    if constexpr (BatchSize < 100) { ... }  // Compiled away
}

void storm_benchmark(int batch_size) {  // Runtime check every call
    if (batch_size < 100) { ... }
}

// ✅ FAIR: Both use runtime values
void storm_benchmark(int batch_size) { ... }
void raw_benchmark(int batch_size) { ... }  // Same decision logic
```

### Benchmark Checklist

- [ ] **Setup outside loop**: WHERE clauses, statement preparation, parameter binding
- [ ] **Same algorithm**: Both use identical strategies (chunked bulk, single row, etc.)
- [ ] **Same containers**: plf::hive vs plf::hive, not plf::hive vs std::vector
- [ ] **Same decision logic**: Runtime vs runtime, not runtime vs compile-time
- [ ] **Correct metric**: Latency for different result sizes, throughput for same sizes
- [ ] **Multiple runs**: 5+ runs to establish variance, report median not just mean

See [Performance Guidelines](docs/development/performance-guidelines.md) for complete rules.

## Common Development Tasks

### Adding a New Database Operation

1. Create statement class in `src/orm/statements/` (inherits `BaseStatement<T>`)
2. Implement single & batch operations
3. Choose return type: INSERT → `std::expected<int64_t/vector<int64_t>, Error>`
4. Consider statement caching pattern (see [Statement Caching](docs/reference/statement-caching.md))
5. Implement compile-time SQL generation
6. Add QuerySet method
7. Add tests in `tests/test_*.cpp`
8. Create performance benchmark

See [Common Tasks](docs/development/common-tasks.md) for detailed patterns.

## Supported Field Types

- **Integer**: `int`, `int64_t`, `long`, `unsigned` variants
- **Floating**: `double`, `float`
- **Boolean**: `bool` (stored as INTEGER 0/1)
- **String**: `std::string`, `const char*`, `std::string_view`
- **Optional**: `std::optional<T>` for any supported type (NULL support)
- **BLOB**: `std::vector<uint8_t>`, `std::vector<unsigned char>`

See [Field Types Reference](docs/reference/field-types.md) for complete mapping.

## Known Compiler Issues

**Module cache corruption**: Simply run build command twice - second attempt succeeds.

```bash
ninja storm_tests  # May fail
ninja storm_tests  # Will succeed
```

Other known issues:
- `std::mutex` segfaults in modules → Use per-thread connections
- `std::function` linker errors → Use abstract base classes
- C headers must be `#include`d, not `import`ed

See [Compiler Issues Reference](docs/reference/compiler-issues.md) for all workarounds.

## Known Issues and Findings

### DISTINCT Performance Analysis (2025-01) - CORRECTED

**Discovery**: The original "rows/sec" throughput metric was **misleading** because it measured output row count, not query latency. The corrected analysis using **latency (ms/query)** reveals the truth:

| Operation | Storm Latency | Raw Latency | Efficiency | Avg Results | True Performance |
|-----------|---------------|-------------|------------|-------------|------------------|
| DISTINCT + WHERE | **0.588ms** | 0.578ms | 98.3% | 78 rows | **FASTEST** - near-parity |
| DISTINCT + WHERE + JOIN | **0.603ms** | 0.811ms | **134.5%** | 2,620 rows | **Very fast** - 1.35x faster |
| DISTINCT + JOIN | 1.143ms | 2.590ms | **226.5%** | 10,000 rows | **Slower, but 2.27x faster than SQLite** |

**Key Insight**: The "rows/sec" metric was misleading because:
- **DISTINCT + JOIN** returns 10,000 rows → inflated throughput (8.75M rows/sec)
- **DISTINCT + WHERE** returns only 78 rows → deflated throughput (0.13M rows/sec)
- **Latency tells the truth**: WHERE is fastest at 0.588ms, JOIN is slowest at 1.143ms

**Why DISTINCT + JOIN shows 226.5% efficiency despite being slower:**
1. **Zero parameter binding** - JOIN conditions are static (`ON sender_id = id`)
2. Raw SQLite's JOIN implementation is inefficient (2.590ms vs Storm's 1.143ms)

**Why DISTINCT + WHERE is fastest:**
- ✅ **Selective WHERE reduces result set** (78 rows vs 10,000 rows)
- ✅ Connection-level `prepare_cached()` handles SQL statement caching

**Benchmark Methodology Note**: Always use **latency (ms/query)** for comparing query performance, not throughput. Throughput is only meaningful when comparing operations with similar result set sizes.

**Caching Architecture:**

DISTINCT uses a simple return-by-value pattern - no thread-local caching at the statement level. All SQL caching is handled at the connection level via `prepare_cached()`:

```cpp
// QuerySet returns DistinctStatement by value
template <std::meta::info... FieldInfos>
auto distinct() {
    return DistinctStatement<T, ConnType, FieldInfos...>{conn_, where_expr_, ...};
}

// Connection handles SQL caching
auto prepare_result = conn_->prepare_cached(sql);  // Hash lookup, reuses existing
```

This is simpler than SELECT's approach (which caches statement pointers locally) but has equivalent performance because the actual statement preparation is cached at the connection level.

### Thread Safety

**✅ Default Connection is Thread-Safe** (Fixed via `thread_local`)

```cpp
// ✅ SAFE: Each thread gets its own connection
void worker_thread() {
    // Initialize thread-local connection
    QuerySet<Person>::set_default_connection(":memory:");

    // Each thread has isolated connection + QuerySet
    QuerySet<Person> qs;
    qs.where(age > 30).select();  // Thread-safe!
}

std::thread t1(worker_thread);
std::thread t2(worker_thread);  // No race - separate connections
```

**⚠️ The following patterns are still NOT thread-safe:**

#### 1. QuerySet Sharing Between Threads

```cpp
// ❌ UNSAFE: Sharing QuerySet between threads
QuerySet<Person> qs;

std::thread t1([&qs]() { qs.where(age > 30).select(); });
std::thread t2([&qs]() { qs.where(age > 50).select(); });  // RACE CONDITION!
```

**Problem**: QuerySet has mutable state (`where_expr_`, `join_stmt_`).
**Race on**: WHERE expression pointer, JOIN statement wrapper.

#### 3. Connection Sharing Between Threads (SQLite Limitation)

```cpp
// ❌ UNSAFE: Sharing connection between threads
auto conn = Connection::create("db.sqlite").value();

std::thread t1([&conn]() { QuerySet<Person>{conn}.select(); });
std::thread t2([&conn]() { QuerySet<Person>{conn}.select(); });  // RACE CONDITION!
```

**Problem**: SQLite connections are not thread-safe.
**Race on**: Statement preparation, execution, internal connection state.

#### Safe Pattern (Use This!)

```cpp
// ✅ SAFE: Per-thread connections and QuerySets
void worker_thread() {
    // Thread-local connection
    auto conn = db::sqlite::Connection::create("database.db").value();

    // Thread-local QuerySet
    QuerySet<Person> qs{conn};

    // Safe - all caching is thread-local
    for (int i = 0; i < 1000; i++) {
        qs.where(age > 30).distinct<^^Person::name>().select();
    }
}

std::thread t1(worker_thread);
std::thread t2(worker_thread);
```

**Why this is safe:**
- Each thread has its own `Connection` instance
- Each thread has its own `QuerySet` instance
- `static thread_local` caching provides isolated storage per thread
- No shared mutable state between threads

#### Remaining Improvements

1. ~~**Make default connection thread_local**~~ ✅ DONE
2. **Document QuerySet thread safety** in public API docs
3. **Consider making QuerySet/Connection non-copyable** to prevent accidental sharing

**Current Status**: Default connection is now thread-safe via `thread_local`. Users must still avoid sharing QuerySet instances across threads.

## Testing

```bash
# Run all tests (104 tests, ~0.5 seconds)
ctest --test-dir build/debug --output-on-failure

# Run specific suite
./build/debug/tests/storm_tests --gtest_filter="SelectTest.*"

# With sanitizers
cmake --preset ninja-debug -DUSE_SANITIZER="address;leak"
cmake --build --preset ninja-debug
ctest --test-dir build/debug
```

See [Testing Strategy](docs/development/testing.md) for comprehensive guide.

## Documentation Structure

- **[docs/architecture/](docs/architecture/)** - Module structure, design decisions, optimizations
- **[docs/development/](docs/development/)** - Getting started, common tasks, testing, performance guidelines
- **[docs/benchmarks/](docs/benchmarks/)** - Performance results, JOIN analysis, DISTINCT analysis
- **[docs/reference/](docs/reference/)** - Field types, statement caching, compiler issues
- **[benchmarks/README.md](benchmarks/README.md)** - Unified benchmark system documentation
- **[rules.md](rules.md)** - General C++23/26 coding standards

## Core Concepts of QuerySet

QuerySet system enables building and executing SQLite queries in a fluent, type-safe manner using C++. Key principles:

### Immutability and Chaining

- **Core Principle**: All non-terminal methods (e.g., `where()`, `join()`) return a **new query object** by copying or moving internal state
- **Fluent API**: Build complex queries in readable, chainable style:
  ```cpp
  auto results = QuerySet<Model>().where(...).order_by<...>().select();
  ```
- **Terminal Methods**: `select()` or standalone aggregates execute immediately and return results

### Clause Ordering

- **Flexible Chaining**: Chain methods in any order for convenience
- **Internal Enforcement**: SQL clauses reordered to match valid SQLite syntax:
  ```
  SELECT ... FROM ... JOIN ... WHERE ... GROUP BY ... HAVING ... ORDER BY ... LIMIT/OFFSET
  ```
- **Validation**: Invalid combinations trigger compile-time errors

### Projection and Result Types

- **Transformers**: `distinct<...>()` or `values<...>()`
- **Without Transformers**: `select()` returns `std::vector<Model>`
- **With Transformers**: `select()` returns `std::vector<std::tuple<...>>` or `std::vector<type>`
- **Standalone Aggregates**: `qs.min<...>()`, `qs.max<...>()` return scalar values

### Available Methods in All Modes

- `join<OtherModel>()`: Adds JOIN clause
- `where(Condition)`: Filters rows
- `order_by<Cols...>()`: Sorts results
- `limit(int)`: Restricts result count
- `offset(int)`: Skips results
- `group_by<Cols...>()`: Groups results
- `having(Condition)`: Filters groups

### Modes and Transformers

QuerySet operates in different **modes** via **transformers**:

**Default Mode (Object Mode)**:
- Begins with `QuerySet<Model>`
- `select()` returns model vectors
- Use transformers to enter Tuple or Aggregate Mode

**Tuple Mode**:
- Entry: `distinct<Cols...>()` or `values<Cols...>()`
- Returns specialized objects with projected columns
- `select()` yields tuples

**Aggregate Mode**:
- Entry: `min<Col>()`, `max<Col>()`, `sum<Col>()`, `avg<Col>()`, `count<Col|*>()`
- Accumulates aggregates
- `select()` yields tuples of aggregate values

**Mode Precedence**:
- Modes combine via chaining transformers
- Final mode determined by: Tuple if projection used, Aggregate if aggregates present
- Prior state transfers to new objects

### GROUP BY Queries

GROUP BY requires an aggregate function - `group_by()` returns `GroupByBuilder` which only has aggregate methods:

```cpp
// Single field GROUP BY with COUNT
qs.group_by<^^Person::department>().count().select();
// Returns: plf::hive<std::tuple<DeptType, int64_t>>

// Multi-field GROUP BY
qs.group_by<^^Person::age, ^^Person::department>().count().select();
// Returns: plf::hive<std::tuple<int, DeptType, int64_t>>

// With SUM aggregate
qs.group_by<^^Person::department>().sum<^^Person::salary>().select();

// Full chain: WHERE + ORDER BY + LIMIT + GROUP BY
qs.where(age > 25)
  .order_by<^^Person::department>()
  .limit(10)
  .group_by<^^Person::department>()
  .count()
  .select();
```

**Available aggregates after `group_by()`:**
- `count()` - COUNT(*)
- `count<^^field>()` - COUNT(field)
- `sum<^^field>()` - SUM(field)
- `avg<^^field>()` - AVG(field)
- `min<^^field>()` - MIN(field)
- `max<^^field>()` - MAX(field)

**Note:** Chaining multiple aggregates (e.g., `.count().sum()`) is not yet supported - use separate queries.

---

**For detailed information, see [docs/](docs/) directory.**
