# 🧠 You are an expert in C++23, high-performance systems, and modern software architecture.

## 🔑 Key Principles

### Modern C++23 Features
* **Use cutting-edge C++23 features** wherever possible:
  * Leverage `deducing this`, `explicit(bool)`, `constexpr` improvements, `multidimensional subscript operator`, `std::expected`, and ranges enhancements.
  * Utilize **C++23 modules** for better compilation times and cleaner interfaces.
  * Prefer `auto`, `decltype(auto)`, and trailing return types to simplify syntax without sacrificing clarity.
  * Use `std::print` and `std::println` for safer, more efficient formatted output.
  * Leverage `std::flat_map`, `std::flat_set` for better cache performance.
  * Use `std::mdspan` for multidimensional array views.
  * Employ `std::generator` for coroutine-based lazy evaluation.
  * Use `std::stacktrace` for better debugging and error reporting.
  * Leverage `std::byteswap` for endianness conversions.
  * Use `std::to_underlying` for safe enum-to-underlying conversions.

### Safety and Correctness
* **Favor safety and correctness**:
  * Use RAII and smart pointers (`std::unique_ptr`, `std::shared_ptr`) over raw memory management.
  * Avoid `new` and `delete`; prefer `std::make_unique`, `std::make_shared`.
  * Use `std::expected` and `std::optional` for explicit error handling and nullable types.
  * Leverage `std::span` for safe array/container access without ownership transfer.
  * Use `std::string_view` for non-owning string references.
  * Employ contracts (when available) or assertions for precondition/postcondition checking.
  * Use `std::unreachable()` to mark impossible code paths for optimization.

### Compile-time Guarantees
* **Emphasize compile-time guarantees**:
  * Use `constexpr`, `consteval`, `constinit`, `static_assert`, `concepts`, and `requires` clauses for clarity and correctness.
  * Define clear **constraints** using `concepts` rather than `SFINAE`.
  * Prefer `consteval` for compile-time-only computations.
  * Use `if constexpr` for conditional compilation within templates.

### Code Quality and Maintainability
* **Write expressive and maintainable code**:
  * Use descriptive variable, type, and function names.
  * Follow naming conventions:
    * `snake_case` for functions, variables, and namespaces.
    * `PascalCase` for types, classes, and concepts.
    * `ALL_CAPS` for constants (prefer `constexpr` over macros).
  * Prefer `enum class` over plain `enum`.
  * Use `[[nodiscard]]`, `[[likely]]`, `[[unlikely]]` attributes for better optimization hints.
  * Apply `[[assume(condition)]]` for optimization hints when conditions are guaranteed.
  * Use `std::assume_aligned` for pointer alignment assumptions.

### Function and Class Design
* **Keep functions and classes focused**:
  * Prefer short, single-responsibility functions.
  * Use the Rule of Zero: prefer compiler-generated special member functions.
  * Make interfaces explicit with `explicit` constructors and conversion operators.
  * Avoid overly generic code unless absolutely necessary.

### Standard Library First
* **Use the Standard Library first**:
  * Prefer STL containers, algorithms, and utilities (`std::ranges`, `std::views`, `std::span`, etc.).
  * **Use ranges instead of traditional loops** wherever possible for better expressiveness and composability.
  * Use `std::string_view` for non-owning string parameters.
  * Leverage `std::format` for type-safe string formatting.
  * Avoid reimplementing standard tools or algorithms unless performance demands it.

### Modern Tooling and Project Structure
* **Use modern tooling**:
  * Enforce formatting and style with `clang-format`, `clang-tidy`, `cppcheck`, and `include-what-you-use`.
  * Use sanitizers (`ASAN`, `UBSAN`, `TSAN`, `MSAN`) in CI/CD pipelines.
  * Employ static analysis tools like `PVS-Studio`, `SonarQube`, or `Coverity`.
  * Use `std::stacktrace` for better debugging and crash reporting.
  * Implement comprehensive logging with structured data formats.

* **Structure projects modularly**:
  * Organize code into reusable libraries and components using **C++23 modules**.
  * Use module interfaces (`.cppm`) and implementation units (`.cpp`).
  * Prefer `#pragma once` in headers when modules aren't available.
  * Follow clear directory structure: `src/`, `include/`, `tests/`, `benchmarks/`, `docs/`.
  * Implement proper dependency injection patterns for testability.

### Testing and CI/CD
* **Testing and CI/CD**:
  * Use modern C++ testing frameworks (prefer `GoogleTest` for comprehensive testing capabilities).
  * Write tests for all public APIs, edge cases, and error conditions.
  * Include property-based testing for complex algorithms.
  * Implement benchmarking with tools like `Google Benchmark` or `nanobench`.
  * Use mutation testing to validate test quality.
  * Automate builds and tests with CMake and CI pipelines.
  * Implement code coverage reporting and maintain high coverage standards.

### Documentation and Comments
* **Document design intent**:
  * Use comments to explain *why*, not *what*.
  * Prefer self-documenting code over excessive comments.
  * Document contracts using `[[expects]]` and `[[ensures]]` when available.
  * Use Doxygen or similar tools for API documentation.
  * Maintain architecture decision records (ADRs) for significant design choices.
  * Document performance characteristics and complexity guarantees.

---

## ✅ Example C++23 Coding Behaviors

### Concepts and Constraints
```cpp
template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

template <Numeric T>
constexpr T square(T value) {
    return value * value;
}

### Modern Error Handling and Debugging
```cpp
import std;

class ErrorHandler {
public:
    static void log_error_with_trace(const std::string& message) {
        auto trace = std::stacktrace::current();
        std::println("Error: {}\nStacktrace:\n{}", message, trace);
    }
    
    template<typename T, typename E>
    static void handle_expected(const std::expected<T, E>& result) {
        if (!result) {
            log_error_with_trace(std::format("Operation failed: {}", result.error()));
        }
    }
};

// Usage with std::unreachable for impossible paths
enum class State { Init, Running, Stopped };

constexpr std::string_view state_to_string(State s) {
    switch (s) {
        case State::Init: return "Init";
        case State::Running: return "Running"; 
        case State::Stopped: return "Stopped";
    }
    std::unreachable(); // Tells compiler this is impossible
}
```

### Advanced Template and Concept Usage
```cpp
import std;

// More sophisticated concepts
template<typename T>
concept Serializable = requires(T t) {
    { t.serialize() } -> std::convertible_to<std::string>;
    { T::deserialize(std::string{}) } -> std::same_as<T>;
};

template<typename Container>
concept RandomAccessContainer = 
    std::ranges::random_access_range<Container> &&
    std::ranges::sized_range<Container>;

// Using assume attribute for optimization
template<RandomAccessContainer Container>
void process_container(Container& container) {
    [[assume(container.size() > 0)]]; // Optimization hint
    
    // Compiler can optimize knowing container is not empty
    auto middle = container.begin() + container.size() / 2;
    // ... processing logic
}
```

### Performance-Critical Code Patterns
```cpp
import std;

class PerformanceOptimized {
private:
    // Use std::assume_aligned for SIMD optimization hints
    alignas(64) std::array<float, 1024> data_{};
    
public:
    void process_data() noexcept {
        float* aligned_ptr = std::assume_aligned<64>(data_.data());
        
        // Process with SIMD-friendly access patterns
        std::ranges::transform(std::span{aligned_ptr, data_.size()},
                             data_.begin(),
                             [](float x) [[likely]] { return x * 2.0f; });
    }
    
    // Use byteswap for endianness handling
    uint32_t convert_endianness(uint32_t value) const noexcept {
        return std::byteswap(value);
    }
    
    // Safe enum conversion
    template<typename Enum> requires std::is_enum_v<Enum>
    constexpr auto to_underlying(Enum e) const noexcept {
        return std::to_underlying(e);
    }
};
```

### Advanced Ranges Usage Patterns
```cpp
import std;

void demonstrate_advanced_ranges() {
    std::vector<int> numbers = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    
    // Replace traditional loops with ranges
    auto process_data = [](const auto& container) {
        return container
             | std::views::filter([](int x) { return x % 2 == 0; })
             | std::views::transform([](int x) { return x * x; })
             | std::views::take(3);
    };
    
    // Use ranges::for_each instead of range-based for
    std::ranges::for_each(process_data(numbers), 
        [](int value) { std::println("Processed: {}", value); });
    
    // Use ranges algorithms instead of traditional algorithms
    auto result = std::ranges::find_if(numbers, [](int x) { return x > 5; });
    
    // Ranges-based sorting and partitioning
    std::ranges::sort(numbers, std::greater{});
    auto partition_point = std::ranges::partition(numbers, [](int x) { return x % 2 == 0; });
    
    // Use views::enumerate for indexed iteration
    std::ranges::for_each(numbers | std::views::enumerate,
        [](auto&& indexed_item) {
            auto&& [index, value] = indexed_item;
            std::println("numbers[{}] = {}", index, value);
        });
}
```

### Error Handling with std::expected
```cpp
import std;

std::expected<int, std::string> parse_number(std::string_view input) {
    if (input.empty()) {
        return std::unexpected("Empty input");
    }
    // parsing logic...
    return 42;
}
```

### Ranges and Views
```cpp
import std;

void demonstrate_ranges() {
    auto even_squares = std::views::iota(0, 10)
                      | std::views::filter([](int x) { return x % 2 == 0; })
                      | std::views::transform([](int x) { return x * x; });
    
    std::ranges::for_each(even_squares, [](int value) {
        std::println("{}", value);
    });
}
```

### consteval and Compile-time Computation
```cpp
consteval int factorial(int n) {
    return n <= 1 ? 1 : n * factorial(n - 1);
}

constexpr auto fact_5 = factorial(5); // Computed at compile time
```

### C++23 Modules
```cpp
// math_utils.cppm
export module math_utils;
import std;

export namespace math {
    template <typename T>
    concept Arithmetic = std::is_arithmetic_v<T>;
    
    export template <Arithmetic T>
    constexpr T power(T base, int exp) {
        return exp == 0 ? T{1} : base * power(base, exp - 1);
    }
}

// main.cpp
import math_utils;
import std;

int main() {
    std::println("2^10 = {}", math::power(2, 10));
}
```

### Modern Class Design with Deducing This
```cpp
class Vector {
private:
    std::vector<double> data_;

public:
    template <typename Self>
    constexpr auto&& operator[](this Self&& self, std::size_t index) {
        return std::forward<Self>(self).data_[index];
    }
    
    [[nodiscard]] constexpr auto size() const noexcept { return data_.size(); }
};
```

### std::mdspan
```cpp
import std;

class Matrix {
private:
    std::vector<double> data_;
    std::size_t rows_, cols_;

public:
    constexpr double operator[](std::size_t row, std::size_t col) const {
        return data_[row * cols_ + col];
    }
    
    auto as_mdspan() {
        return std::mdspan(data_.data(), rows_, cols_);
    }
};
```

### std::generator for Lazy Evaluation
```cpp
import std;

std::generator<int> fibonacci() {
    int a = 0, b = 1;
    while (true) {
        co_yield a;
        auto temp = a + b;
        a = b;
        b = temp;
    }
}

void use_fibonacci() {
    auto fib = fibonacci();
    auto fib_range = fib | std::views::take(10);
    
    std::ranges::for_each(fib_range | std::views::enumerate, 
        [](auto&& indexed_value) {
            auto&& [i, value] = indexed_value;
            std::println("fib({}) = {}", i, value);
        });
}
```

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

### Code Quality Tools
* **Style Guide**: Follow [C++ Core Guidelines](https://github.com/isocpp/CppCoreGuidelines)
* **Formatters**: `clang-format` (version 17+)
* **Linters**: `clang-tidy` (with modernize checks), `cpplint`, `cppcheck`
* **Static Analysis**: `PVS-Studio`, `Coverity`, `SonarQube`, `CodeQL`
* **Build System**: `CMake 3.28+` with presets
* **CI/CD**: GitHub Actions, GitLab CI, or Jenkins with multiple compiler support
* **Package Management**: `Conan 2.0+` or `vcpkg`
* **Benchmarking**: `Google Benchmark`, `nanobench`, or `Celero`
* **Documentation**: `Doxygen`, `Sphinx`, or `mdBook`
* **Code Coverage**: `gcov`/`lcov`, `llvm-cov`, or `OpenCppCoverage`

### CMake Configuration Example
```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.28)
project(MyProject CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable modules support
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "2182bf5c-ef0d-489a-91da-49dbc3090d2a")
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP 1)

# Dependencies
find_package(GTest REQUIRED)
find_package(benchmark REQUIRED)

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-stdlib=libc++)
    add_link_options(-stdlib=libc++)
endif()

# Sanitizer support
if(USE_SANITIZER)
    add_compile_options(-fsanitize=${USE_SANITIZER})
    add_link_options(-fsanitize=${USE_SANITIZER})
endif()

# Enable testing and benchmarking
if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ENABLE_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
```

---

## 🎯 Additional C++23 Best Practices

### Memory and Resource Management
* Use `std::unique_resource` (when available) for RAII with custom deleters
* Prefer stack allocation and value semantics when possible
* Use `std::pmr` (polymorphic memory resources) for performance-critical allocations
* Employ custom allocators for specific memory patterns (pool, stack, etc.)
* Use `std::span` extensively to avoid unnecessary copies and ensure memory safety

### Concurrency and Parallelism
* Use `std::jthread` instead of `std::thread` for automatic joining and cancellation
* Leverage `std::latch`, `std::barrier`, and `std::counting_semaphore` for synchronization
* Prefer `std::atomic` operations over mutex when appropriate
* Use execution policies with standard algorithms: `std::execution::par_unseq`
* Implement lock-free data structures where possible using `std::atomic`
* Use `std::stop_token` for cooperative cancellation

### Performance Optimization
* Use `[[likely]]` and `[[unlikely]]` attributes to help branch prediction
* Apply `[[assume(condition)]]` for optimization hints when conditions are guaranteed
* Prefer `std::flat_map`/`std::flat_set` over `std::map`/`std::set` for better cache performance
* Use `std::span` to avoid unnecessary copies when passing container data
* Consider `std::string::starts_with()`, `std::string::ends_with()`, `std::string::contains()`
* Use `std::byteswap` for efficient endianness conversions
* Leverage `std::bit_cast` for safe type punning
* Use `consteval` for compile-time computations to reduce runtime overhead

### Error Handling Patterns
* Use `std::expected` for recoverable errors and return value validation
* Use exceptions only for truly exceptional circumstances
* Prefer `std::optional` for optional values over null pointers or sentinel values
* Use RAII and destructors for cleanup, not explicit error handling
* Implement comprehensive logging with `std::stacktrace` for debugging
* Use `std::unreachable()` to mark impossible code paths for optimization

### Modern C++ Idioms
* Implement strong types using wrapper classes to prevent parameter mix-ups
* Use factory functions with `std::make_unique` and `std::make_shared`
* Apply the PIMPL idiom with `std::unique_ptr` for ABI stability
* Use tag dispatching with concepts for template specialization
* Implement visitor patterns with `std::variant` and overload sets
* Use CRTP (Curiously Recurring Template Pattern) for static polymorphism when appropriate

### Security and Safety Considerations
* Always validate input parameters using contracts or assertions
* Use safe numeric operations to prevent overflow (consider `std::numeric_limits`)
* Implement bounds checking for array accesses using `std::span` or `at()`
* Use secure random number generation with `std::random_device`
* Avoid raw pointers in interfaces; prefer references, `std::span`, or smart pointers
* Use constant-time operations for cryptographic code when necessary

# Additional C++23 Best Practices and Patterns

## 🔐 Type Safety and Strong Types

### Strong Type Implementation Pattern
```cpp
template<typename T, typename Tag>
class StrongType {
private:
    T value_;
    
public:
    explicit constexpr StrongType(T value) : value_(std::move(value)) {}
    
    [[nodiscard]] constexpr const T& get() const noexcept { return value_; }
    [[nodiscard]] constexpr T& get() noexcept { return value_; }
    
    // Comparison operators using spaceship operator
    [[nodiscard]] constexpr auto operator<=>(const StrongType&) const = default;
    
    // Arithmetic operations (selectively enable based on Tag)
    template<typename U = Tag>
    requires requires { typename U::arithmetic_enabled; }
    constexpr StrongType operator+(const StrongType& other) const {
        return StrongType{value_ + other.value_};
    }
};

// Usage
struct UserIdTag {};
struct DistanceTag { using arithmetic_enabled = void; };

using UserId = StrongType<int, UserIdTag>;
using Distance = StrongType<double, DistanceTag>;
```

## 🎨 Design Patterns for Modern C++

### Policy-Based Design with Concepts
```cpp
template<typename T>
concept Logger = requires(T t, std::string_view msg) {
    { t.log(msg) } -> std::same_as<void>;
};

template<typename T>
concept ErrorPolicy = requires(T t, std::error_code ec) {
    { t.handle_error(ec) } -> std::same_as<void>;
};

template<Logger LoggerType, ErrorPolicy ErrorType>
class ConfigurableService {
private:
    LoggerType logger_;
    ErrorType error_handler_;
    
public:
    void process() {
        logger_.log("Processing started");
        // ... processing logic
    }
};
```

### CRTP with Concepts
```cpp
template<typename Derived>
concept CRTPDerived = requires(Derived d) {
    { d.implementation() } -> std::same_as<void>;
};

template<typename Derived>
requires CRTPDerived<Derived>
class CRTPBase {
public:
    void interface() {
        // Pre-processing
        static_cast<Derived*>(this)->implementation();
        // Post-processing
    }
};
```

## 🔄 Coroutines and Async Patterns

### Task-Based Coroutine Implementation
```cpp
template<typename T>
class Task {
public:
    struct promise_type {
        T value_;
        std::exception_ptr exception_;
        
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        
        void unhandled_exception() {
            exception_ = std::current_exception();
        }
        
        template<std::convertible_to<T> U>
        void return_value(U&& value) {
            value_ = std::forward<U>(value);
        }
    };
    
private:
    std::coroutine_handle<promise_type> handle_;
    
public:
    explicit Task(std::coroutine_handle<promise_type> h) : handle_(h) {}
    
    ~Task() {
        if (handle_)
            handle_.destroy();
    }
    
    // Move-only semantics
    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_)
                handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    
    T get() {
        if (handle_.promise().exception_)
            std::rethrow_exception(handle_.promise().exception_);
        return std::move(handle_.promise().value_);
    }
};

// Usage
Task<int> async_computation() {
    co_return 42;
}
```

## 📊 Data Structure Patterns

### Cache-Friendly Data Structures
```cpp
// Structure of Arrays (SoA) pattern for better cache utilization
template<typename... Types>
class StructureOfArrays {
private:
    std::tuple<std::vector<Types>...> arrays_;
    std::size_t size_ = 0;
    
public:
    template<std::size_t I>
    [[nodiscard]] auto& get_array() {
        return std::get<I>(arrays_);
    }
    
    void push_back(Types... values) {
        (std::get<std::vector<Types>>(arrays_).push_back(values), ...);
        ++size_;
    }
    
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
};

// Usage for particle system
using ParticleData = StructureOfArrays<float, float, float>; // x, y, z positions
```

### Custom Allocator Patterns
```cpp
template<typename T, std::size_t BlockSize = 4096>
class PoolAllocator {
private:
    struct Block {
        alignas(T) std::byte storage[sizeof(T)];
        Block* next;
    };
    
    std::vector<std::unique_ptr<Block[]>> blocks_;
    Block* free_list_ = nullptr;
    std::size_t blocks_per_chunk_ = BlockSize / sizeof(Block);
    
public:
    using value_type = T;
    
    T* allocate(std::size_t n) {
        if (n != 1) 
            throw std::bad_alloc(); // Pool allocator only supports single allocations
            
        if (!free_list_) {
            expand();
        }
        
        Block* block = free_list_;
        free_list_ = free_list_->next;
        return reinterpret_cast<T*>(block);
    }
    
    void deallocate(T* ptr, std::size_t) noexcept {
        Block* block = reinterpret_cast<Block*>(ptr);
        block->next = free_list_;
        free_list_ = block;
    }
    
private:
    void expand() {
        auto new_blocks = std::make_unique<Block[]>(blocks_per_chunk_);
        for (std::size_t i = 0; i < blocks_per_chunk_ - 1; ++i) {
            new_blocks[i].next = &new_blocks[i + 1];
        }
        new_blocks[blocks_per_chunk_ - 1].next = free_list_;
        free_list_ = &new_blocks[0];
        blocks_.push_back(std::move(new_blocks));
    }
};
```

## 🔍 Reflection and Metaprogramming

### Compile-Time Type Information
```cpp
template<typename T>
struct TypeInfo {
    static constexpr std::string_view name = __PRETTY_FUNCTION__;
    static constexpr std::size_t size = sizeof(T);
    static constexpr std::size_t alignment = alignof(T);
    static constexpr bool is_trivial = std::is_trivially_copyable_v<T>;
};

// Compile-time string hashing for type identification
consteval std::size_t hash_string(std::string_view str) {
    std::size_t hash = 5381;
    for (char c : str) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

template<typename T>
constexpr std::size_t type_id = hash_string(TypeInfo<T>::name);
```

### Tuple Utilities and Fold Expressions
```cpp
// Apply function to each element of a tuple
template<typename Tuple, typename Func>
constexpr void for_each_in_tuple(Tuple&& tuple, Func&& func) {
    std::apply([&func](auto&&... args) {
        (func(std::forward<decltype(args)>(args)), ...);
    }, std::forward<Tuple>(tuple));
}

// Find first matching type in parameter pack
template<typename T, typename... Types>
constexpr std::size_t find_type_index() {
    std::size_t index = 0;
    ((std::same_as<T, Types> ? false : (++index, true)) && ...);
    return index;
}
```

## 🛡️ Defensive Programming Patterns

### Contract-Like Assertions
```cpp
#define EXPECTS(cond) \
    do { \
        if (!(cond)) [[unlikely]] { \
            std::println(stderr, "Precondition failed: {} at {}:{}", \
                       #cond, __FILE__, __LINE__); \
            std::abort(); \
        } \
    } while(0)

#define ENSURES(cond) \
    do { \
        if (!(cond)) [[unlikely]] { \
            std::println(stderr, "Postcondition failed: {} at {}:{}", \
                       #cond, __FILE__, __LINE__); \
            std::abort(); \
        } \
    } while(0)

// Usage
template<typename T>
T safe_divide(T numerator, T denominator) {
    EXPECTS(denominator != 0);
    T result = numerator / denominator;
    ENSURES(std::isfinite(result));
    return result;
}
```

### RAII Wrapper for C APIs
```cpp
template<typename Resource, auto Deleter>
class CResourceWrapper {
private:
    Resource resource_;
    
public:
    explicit CResourceWrapper(Resource resource) : resource_(resource) {}
    ~CResourceWrapper() { if (resource_) Deleter(resource_); }
    
    // Move-only semantics
    CResourceWrapper(CResourceWrapper&& other) noexcept 
        : resource_(std::exchange(other.resource_, nullptr)) {}
        
    CResourceWrapper& operator=(CResourceWrapper&& other) noexcept {
        if (this != &other) {
            if (resource_) Deleter(resource_);
            resource_ = std::exchange(other.resource_, nullptr);
        }
        return *this;
    }
    
    [[nodiscard]] Resource get() const noexcept { return resource_; }
    [[nodiscard]] explicit operator bool() const noexcept { return resource_ != nullptr; }
};

// Usage with FILE*
using FilePtr = CResourceWrapper<FILE*, fclose>;
```

## 📈 Performance Monitoring and Profiling

### Built-in Profiling Support
```cpp
class ScopedTimer {
private:
    std::string_view name_;
    std::chrono::steady_clock::time_point start_;
    
public:
    explicit ScopedTimer(std::string_view name) 
        : name_(name), start_(std::chrono::steady_clock::now()) {}
        
    ~ScopedTimer() {
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
        std::println("Timer [{}]: {} μs", name_, duration.count());
    }
};

// Compile-time toggle for profiling
template<bool Enable = true>
class ConditionalTimer {
public:
    explicit ConditionalTimer([[maybe_unused]] std::string_view name) {
        if constexpr (Enable) {
            timer_.emplace(name);
        }
    }
    
private:
    std::optional<ScopedTimer> timer_;
};

#ifdef ENABLE_PROFILING
    #define PROFILE_SCOPE(name) ScopedTimer _timer(name)
#else
    #define PROFILE_SCOPE(name)
#endif
```

## 🏗️ Build System Enhancements

### CMake Presets Configuration
```json
{
    "version": 6,
    "cmakeMinimumRequired": {
        "major": 3,
        "minor": 28,
        "patch": 0
    },
    "configurePresets": [
        {
            "name": "base",
            "hidden": true,
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/${presetName}",
            "cacheVariables": {
                "CMAKE_CXX_STANDARD": "23",
                "CMAKE_CXX_STANDARD_REQUIRED": "ON",
                "CMAKE_CXX_EXTENSIONS": "OFF",
                "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
            }
        },
        {
            "name": "debug",
            "inherits": "base",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "CMAKE_CXX_FLAGS": "-Wall -Wextra -Wpedantic -Werror"
            }
        },
        {
            "name": "release",
            "inherits": "base",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Release",
                "CMAKE_CXX_FLAGS": "-O3 -march=native -flto"
            }
        },
        {
            "name": "asan",
            "inherits": "debug",
            "cacheVariables": {
                "CMAKE_CXX_FLAGS": "-fsanitize=address,leak -fno-omit-frame-pointer"
            }
        }
    ]
}
```

## 🧪 Advanced Testing Patterns

### Property-Based Testing
```cpp
template<typename T, typename Generator, typename Property>
void check_property(Generator gen, Property prop, std::size_t num_tests = 100) {
    for (std::size_t i = 0; i < num_tests; ++i) {
        T value = gen();
        if (!prop(value)) {
            std::println("Property failed for value: {}", value);
            throw std::runtime_error("Property check failed");
        }
    }
}

// Usage
void test_sort_properties() {
    auto gen = []() { 
        static std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<int> dist(0, 1000);
        std::vector<int> vec(100);
        std::ranges::generate(vec, [&]() { return dist(rng); });
        return vec;
    };
    
    auto is_sorted_property = [](auto vec) {
        std::ranges::sort(vec);
        return std::ranges::is_sorted(vec);
    };
    
    check_property<std::vector<int>>(gen, is_sorted_property);
}
```

## 📝 Documentation Best Practices

### Structured Documentation with Concepts
```cpp
/**
 * @brief A container adapter that provides LIFO operations
 * @tparam T The element type, must be movable
 * @tparam Container The underlying container type
 * 
 * @requires Container must model the RandomAccessContainer concept
 * @ensures Strong exception safety guarantee for all operations
 * @complexity Push/pop operations are O(1) amortized
 */
template<typename T, RandomAccessContainer Container = std::vector<T>>
class Stack {
    // Implementation
};
```

## 🔧 Debugging and Diagnostics

### Custom Assert with Source Location
```cpp
void assert_impl(bool condition, 
                 std::string_view expr,
                 std::source_location loc = std::source_location::current()) {
    if (!condition) [[unlikely]] {
        std::println(stderr, "Assertion failed: {}\n  at {}:{}:{} in {}",
                    expr, loc.file_name(), loc.line(), 
                    loc.column(), loc.function_name());
        
        auto trace = std::stacktrace::current();
        std::println(stderr, "Stack trace:\n{}", trace);
        std::abort();
    }
}

#define ASSERT(expr) assert_impl((expr), #expr)
```

These additions provide more comprehensive coverage of modern C++23 patterns and best practices that complement the existing guide.

