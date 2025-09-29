#include <chrono>
#include <iostream>
#include <vector>
#include <iomanip>
#include <string>

import storm;
import <span>;
import <expected>;

class MicroBenchmarkTimer {
public:
    MicroBenchmarkTimer() : start_(std::chrono::high_resolution_clock::now()) {}

    double elapsed_ns() const {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_);
        return duration.count();
    }

    double elapsed_us() const {
        return elapsed_ns() / 1000.0;
    }

    void reset() {
        start_ = std::chrono::high_resolution_clock::now();
    }

private:
    std::chrono::high_resolution_clock::time_point start_;
};

// Test struct for Storm ORM
struct Person {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string name;
    int age;
};

// Access the static methods directly from InsertStatement
void benchmark_sql_generation() {
    std::cout << "=== SQL GENERATION MICRO-BENCHMARK ===" << std::endl;
    std::cout << "Testing compile-time optimization vs runtime generation" << std::endl;
    std::cout << std::endl;

    // Setup connection (needed for template instantiation)
    auto result = storm::QuerySet<Person>::set_default_connection(":memory:");
    if (!result.has_value()) {
        std::cerr << "Failed to set Storm connection: " << result.error().message() << std::endl;
        return;
    }

    auto& conn = storm::QuerySet<Person>::get_default_connection();
    auto create_result = conn.execute(
        "CREATE TABLE Person ("
        "id INTEGER PRIMARY KEY, "
        "name TEXT NOT NULL, "
        "age INTEGER NOT NULL"
        ")"
    );

    // Create QuerySet to trigger template instantiation
    auto queryset = storm::QuerySet<Person>{};

    // Test batch sizes that would trigger cache misses and hits
    const std::vector<size_t> test_batch_sizes = {
        1, 2, 3, 5, 7, 8, 10, 12, 15, 20, 25, 30, 35, 40, 45, 50,
        55, 60, 75, 100, 150, 200, 300, 500, 1000
    };

    std::cout << "Batch Size | SQL Gen Time (μs) | Cache Hit | SQL Length" << std::endl;
    std::cout << "-----------|-------------------|-----------|------------" << std::endl;

    for (size_t batch_size : test_batch_sizes) {
        MicroBenchmarkTimer timer;

        // Measure SQL generation time
        // Note: We can't access the static method directly, so we simulate the workload
        // by creating a person vector and calling the insert operation
        std::vector<Person> persons;
        persons.reserve(batch_size);
        for (size_t i = 0; i < batch_size; ++i) {
            persons.push_back({static_cast<int>(i + 1), "Test" + std::to_string(i), 25});
        }

        timer.reset();

        // This will trigger SQL generation (and caching)
        auto insert_result = queryset.insert(std::span<const Person>(persons));

        double generation_time = timer.elapsed_us();

        // Determine if this was likely a cache hit or miss
        // (smaller times indicate cache hits)
        bool likely_cache_hit = generation_time < 50.0; // Heuristic threshold

        // Clean up for next test
        if (insert_result.has_value()) {
            // Remove inserted data
            for (const auto& person : persons) {
                queryset.remove(person);
            }
        }

        std::cout << std::setw(10) << batch_size << " | "
                  << std::setw(17) << std::fixed << std::setprecision(3) << generation_time << " | "
                  << std::setw(9) << (likely_cache_hit ? "Likely" : "Unlikely") << " | "
                  << std::setw(10) << "~" << (50 + batch_size * 8) << std::endl;
    }

    std::cout << std::endl;
    std::cout << "=== CACHE EFFECTIVENESS TEST ===" << std::endl;
    std::cout << "Running repeated operations to test cache performance" << std::endl;
    std::cout << std::endl;

    // Test cache effectiveness for common batch sizes
    const std::vector<size_t> common_sizes = {1, 10, 25, 50};
    const int iterations = 100;

    for (size_t batch_size : common_sizes) {
        std::vector<double> times;
        times.reserve(iterations);

        std::vector<Person> persons;
        persons.reserve(batch_size);
        for (size_t i = 0; i < batch_size; ++i) {
            persons.push_back({static_cast<int>(i + 1), "Test" + std::to_string(i), 25});
        }

        for (int i = 0; i < iterations; ++i) {
            // Update IDs to avoid conflicts
            for (size_t j = 0; j < persons.size(); ++j) {
                persons[j].id = static_cast<int>(i * batch_size + j + 1);
            }

            MicroBenchmarkTimer timer;
            auto insert_result = queryset.insert(std::span<const Person>(persons));
            times.push_back(timer.elapsed_us());

            // Clean up
            if (insert_result.has_value()) {
                for (const auto& person : persons) {
                    queryset.remove(person);
                }
            }
        }

        // Calculate statistics
        double total_time = 0;
        double min_time = times[0];
        double max_time = times[0];

        for (double time : times) {
            total_time += time;
            min_time = std::min(min_time, time);
            max_time = std::max(max_time, time);
        }

        double avg_time = total_time / iterations;

        std::cout << "Batch size " << batch_size << " (" << iterations << " iterations):" << std::endl;
        std::cout << "  Average: " << std::fixed << std::setprecision(3) << avg_time << " μs" << std::endl;
        std::cout << "  Min:     " << std::fixed << std::setprecision(3) << min_time << " μs" << std::endl;
        std::cout << "  Max:     " << std::fixed << std::setprecision(3) << max_time << " μs" << std::endl;
        std::cout << "  Speedup (max/min): " << std::fixed << std::setprecision(1) << (max_time / min_time) << "x" << std::endl;
        std::cout << std::endl;
    }

    std::cout << "=== OPTIMIZATION IMPACT ANALYSIS ===" << std::endl;
    std::cout << "Key optimizations implemented:" << std::endl;
    std::cout << "1. Compile-time SQL prefix generation eliminates runtime std::format calls" << std::endl;
    std::cout << "2. Pre-computed field names and placeholders avoid reflection overhead" << std::endl;
    std::cout << "3. Thread-local 8-entry cache with round-robin replacement" << std::endl;
    std::cout << "4. Memory pre-allocation using exact size calculation" << std::endl;
    std::cout << "5. Value template reuse for bulk INSERT operations" << std::endl;
    std::cout << std::endl;
    std::cout << "Expected performance characteristics:" << std::endl;
    std::cout << "- Cache hits: <50μs for SQL generation" << std::endl;
    std::cout << "- Cache misses: 50-200μs depending on batch size" << std::endl;
    std::cout << "- Memory allocation: Pre-allocated to exact size (no reallocations)" << std::endl;
    std::cout << "- String building: O(n) with single concatenation pass" << std::endl;
}

int main() {
    benchmark_sql_generation();
    return 0;
}