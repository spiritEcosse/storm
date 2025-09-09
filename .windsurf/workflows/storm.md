---
description: 
auto_execution_mode: 1
---

# Storm — Modern C++26 ORM (development guidance)

**Storm** is a lightweight, modular ORM targeting C++26. It emphasizes clear separation of concerns, small replaceable modules (backends, query builders, transaction layers), and modern idioms to keep code fast, safe, and maintainable. Below are the recommended principles, build commands, and examples for contributors.

## Quick build (development)

Development build with tests and sanitizers:

```bash
# Development build with tests and sanitizers
cmake --preset=ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="address;leak" && cmake --build --preset=ninja-debug
```

Run tests with detailed output:

```bash
cmake --preset=ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="address;leak" \
  && cmake --build --preset=ninja-debug \
  && (cd build/debug && ctest -j 32 --output-on-failure -V; cd ../../)
```

---

## Key principles (C++26 adaptation)

### Embrace C++26 features

Storm is designed as a showcase of modern C++26:

* **Modules** as the primary boundary between components (`storm.core`, `storm.sqlite`).
* **Static reflection** (`std::meta`) to automatically map entities to tables and columns without hand-written boilerplate.
* **`constexpr` and `consteval`** for compile-time query generation and validation.
* **Concepts and requires** clauses for clear API contracts.
* **`explicit(bool)` and deducing `this`** to improve ergonomics.
* **`std::generator` and ranges** for streaming results.
* **Variadic templates** for flexible query composition (`where(col, op, value...)`).
* **Monadic composition** (`and_then`, `transform`, `or_else`) with `std::expected`.
* **Functional idioms**: higher-order functions, range adaptors, and pipelines.

### Safety and correctness

* **No exceptions**. Storm does not throw. All operations use `std::expected<T, DbError>` for error propagation.
* **RAII** everywhere — connections, statements, transactions, and buffers.
* **\[\[nodiscard]]** on important return values to prevent silent error ignoring.
* **Optionals for nullable values**, never sentinel values.
* **Compile-time guarantees** for schema mapping, query building, and invariants.
* **Do not use `virtual` functions** — prefer **concepts**, **CRTP**, or **tag dispatch**. This removes vtable overhead, improves inlining, and makes the library more modular.

### Reflection and entity mapping

C++26 meta reflection allows Storm to remove boilerplate:

```cpp
struct User {
    int id;
    std::string name;
    std::string email;
};

// Reflection-based schema mapping
constexpr auto schema = storm::reflect<User>();
// Produces table "User" with columns: id, name, email
```

Advantages:

* Compile-time schema mapping.
* Automatic `serialize` / `deserialize`.
* Eliminates hand-written SQL bindings.

### Monadic style

Storm APIs are monadic-first:

```cpp
auto res = conn.execute(q)
    .and_then([](auto row) { return User::deserialize(row); })
    .or_else([](DbError e) { log_error(e); return std::unexpected(e); });
```

This eliminates exceptions, makes failure explicit, and enables fluent chaining.

### Variadic and functional patterns

Variadic templates power flexible APIs:

```cpp
auto q = select("id", "name", "email").from("users").where("id = ?", 42);
```

Generic variadic functions handle parameter binding seamlessly:

```cpp
template <typename... Args>
auto bind_params(Statement& stmt, Args&&... args);
```

---

## Project structure

```
/docs
/include          # module interfaces (.cppm) & fallback headers
/src              # implementation units
/tests            # GoogleTest tests
/benchmarks       # Google Benchmark
/examples
/cmake
/CMakePresets.json
```

Prefer module-based boundaries:

```
import storm.core;       // public query API
import storm.sqlite;     // SQLite backend module
import storm.pg;         // optional Postgres backend module
```

---

## Testing & CI

* **GoogleTest** for unit and integration tests.
* **Property-based tests** for query builder correctness and SQL generation.
* **Sanitizers** (ASAN, UBSAN, TSAN) enabled in CI.
* **Coverage** builds with llvm-cov.
* **No exception checks** — since library never throws, tests assert explicit error handling via `std::expected`.

---

## API design recommendations

### Small, orthogonal components

* Split responsibilities: `Connection`, `Transaction`, `QueryBuilder`, `Mapper`, `Backend`.
* Each backend implements a small interface (connection pooling, statement execution, parameter binding).
* **Never use inheritance + virtual** for backends — instead provide **concepts** and let each backend satisfy them.

### Concepts and traits

```cpp
template<typename T>
concept Entity = requires(T t) {
    { T::table_name() } -> std::convertible_to<std::string_view>;
    { storm::reflect<T>() } -> storm::Schema;
};
```

### Error handling (no exceptions)

```cpp
std::expected<Row, DbError> fetch_one(Query q);
std::expected<void, DbError> insert(User u);
```

---

## Performance patterns

* **`std::flat_map`/`flat_set`** for metadata lookup.
* **`std::mdspan`** for binary blobs and efficient buffer handling.
* **`std::assume_aligned`** for SIMD-friendly memory.
* **Compile-time query plans** using reflection + constexpr.
* **No `virtual` dispatch in hot paths** — prefer static polymorphism (CRTP).
* **Avoid runtime type erasure unless unavoidable.**

---

## Examples

### Entity with reflection

```cpp
struct User {
    int id;
    std::string name;
    std::string email;
};

constexpr auto schema = storm::reflect<User>();
```

### Query with monadic API

```cpp
auto q = select("*").from("users").where("id = ?", 1);

auto user = conn.execute(q)
    .and_then(User::deserialize)
    .or_else([](DbError e) { return std::unexpected(e); });
```

---

## Contribution guidelines (short)

* Write modular, exception-free code.
* **Never introduce `virtual` functions.**
* Use reflection whenever possible instead of hand-coded mappings.
* Add tests for all public APIs.
* Benchmark changes touching query execution.
* Document public APIs with Doxygen-style comments.
* Keep PRs small and focused.

---

## Closing notes

Storm is designed as an **exception-free**, **reflection-driven**, **monadic ORM** for C++26. It favors compile-time validation, functional composition, and explicit error handling over exceptions. By **avoiding virtual functions**, leveraging **modules**, **ranges**, **generators**, **meta reflection**, and **monadic patterns**, Storm provides both **safety** and **performance** while staying highly **extensible** across different database backends.
