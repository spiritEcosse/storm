---
trigger: always_on
---

Here is a professional **AI instruction prompt template** for a coding assistant focused on **C++23** and scalable software design:

---

## 🧠 You are an expert in C++23, high-performance systems, and modern software architecture.

### 🔑 Key Principles

* **Use modern C++23 features** wherever possible:

  * Leverage `deducing this`, `explicit(bool)`, `constexpr` improvements, `multidimensional subscript operator`, `std::expected`, and ranges enhancements.
  * Prefer `auto`, `decltype(auto)`, and trailing return types to simplify syntax without sacrificing clarity.

* **Favor safety and correctness**:

  * Use RAII and smart pointers (`std::unique_ptr`, `std::shared_ptr`) over raw memory management.
  * Avoid `new` and `delete`; prefer `std::make_unique`, `std::make_shared`.

* **Emphasize compile-time guarantees**:

  * Use `constexpr`, `consteval`, `static_assert`, `concepts`, and `requires` clauses for clarity and correctness.
  * Define clear **constraints** using `concepts` rather than `SFINAE`.

* **Write expressive and maintainable code**:

  * Use descriptive variable, type, and function names.
  * Follow naming conventions:

    * `snake_case` for functions and variables.
    * `CamelCase` for types and classes.
    * `ALL_CAPS` for constants or macros (though macros should be avoided).

* **Keep functions and classes focused**:

  * Prefer short, single-responsibility functions.
  * Avoid overly generic code unless absolutely necessary.

* **Use the Standard Library first**:

  * Prefer STL containers, algorithms, and utilities (`std::ranges`, `std::views`, `std::span`, etc.).
  * Avoid reimplementing standard tools or algorithms unless performance demands it.

* **Use modern tooling**:

  * Enforce formatting and style with `clang-format`, `clang-tidy`, `cppcheck`, and `include-what-you-use`.
  * Use sanitizers (`ASAN`, `UBSAN`, `TSAN`) in CI.

* **Structure projects modularly**:

  * Organize code into reusable libraries and components.
  * One `.h` file per interface; `.cpp` file per implementation.
  * Use `#pragma once` instead of traditional include guards.

* **Testing and CI/CD**:

  * Use modern C++ testing frameworks (`Catch2`, `doctest`, or `GoogleTest`).
  * Write tests for all public APIs and edge cases.
  * Automate builds and tests with CMake and CI pipelines (e.g., GitHub Actions, GitLab CI).

* **Document design intent**:

  * Use comments to explain *why*, not *what*.
  * Prefer self-documenting code over excessive comments.

---

### ✅ Example C++23 Coding Behaviors

* ✅ Use concepts:

  ```cpp
  template <typename T>
    requires std::integral<T>
  T square(T value) {
      return value * value;
  }
  ```

* ✅ Use `std::expected` for safer error handling:

  ```cpp
  std::expected<int, std::string> parse_number(std::string_view input);
  ```

* ✅ Use `ranges` and views:

  ```cpp
  for (int value : std::views::iota(0, 10) | std::views::filter([](int x){ return x % 2 == 0; }))
      std::cout << value << '\n';
  ```

* ✅ Use `consteval`:

  ```cpp
  consteval int factorial(int n) {
      return n <= 1 ? 1 : n * factorial(n - 1);
  }
  ```

---

### 🧷 Formatting and Tools

* **Style**: Follow [C++ Core Guidelines](https://github.com/isocpp/CppCoreGuidelines)
* **Linters**: `clang-tidy`, `cpplint`
* **Formatters**: `clang-format`
* **CI**: Use `CMake`, `CTest`, GitHub Actions
* **Static analysis**: `cppcheck`, `include-what-you-use`

---


## 🧷 Build Commands and Tooling

### Build Configuration
```bash
# Development build with tests and sanitizers
cmake --preset=ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="address;leak" && cmake --build --preset=ninja-debug

# Run tests with detailed output
cmake --preset=ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="address;leak" && cmake --build --preset=ninja-debug && (cd build/debug && ctest -j 32 --output-on-failure -V; cd ../../)

# Release build for production
cmake --preset=ninja-release -DENABLE_TESTS=OFF && cmake --build --preset=ninja-release

# Build with comprehensive sanitizers and tools
cmake --preset=ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="address;leak;undefined" && cmake --build --preset=ninja-debug

# Build with thread sanitizer (separate build due to incompatibility)
cmake --preset=ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="thread" && cmake --build --preset=ninja-debug

# Build with memory sanitizer
cmake --preset=ninja-debug -DENABLE_TESTS=ON -DUSE_SANITIZER="memory" && cmake --build --preset=ninja-debug

# Profile-guided optimization build
cmake --preset=ninja-pgo -DENABLE_PGO=ON && cmake --build --preset=ninja-pgo

# Build with code coverage
cmake --preset=ninja-coverage -DENABLE_COVERAGE=ON && cmake --build --preset=ninja-coverage && (cd build/coverage && ctest && lcov --capture --directory . --output-file coverage.info && genhtml coverage.info --output-directory coverage_html; cd ../../)
```
