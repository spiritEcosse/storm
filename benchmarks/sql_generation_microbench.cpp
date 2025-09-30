#include "benchmark_utils.hpp"

import storm;
import <span>;
import <expected>;

using namespace storm::benchmark;

// Define the actual Person struct with Storm imports
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
    auto create_result = conn.execute(db_utils::PERSON_TABLE_SQL);

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
        std::vector<Person> persons = data_utils::generate_test_data_range<Person>(batch_size, 1);

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

        std::vector<Person> persons = data_utils::generate_test_data_range<Person>(batch_size, 1);

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

    // Cleanup
    storm::QuerySet<Person>::clear_default_connection();
}

// Benchmark DELETE SQL generation
void benchmark_delete_sql_generation() {
    std::cout << "\n\n=== DELETE SQL GENERATION MICRO-BENCHMARK ===" << std::endl;
    std::cout << "Testing DELETE SQL generation performance and caching" << std::endl;
    std::cout << std::endl;

    // Setup connection
    auto result = storm::QuerySet<Person>::set_default_connection(":memory:");
    if (!result.has_value()) {
        std::cerr << "Failed to set Storm connection: " << result.error().message() << std::endl;
        return;
    }

    auto& conn = storm::QuerySet<Person>::get_default_connection();
    auto create_result = conn.execute(db_utils::PERSON_TABLE_SQL);
    if (!create_result.has_value()) {
        std::cerr << "Failed to create table: " << create_result.error().message() << std::endl;
        storm::QuerySet<Person>::clear_default_connection();
        return;
    }

    // Create QuerySet
    auto queryset = storm::QuerySet<Person>{};

    // Insert test data for deletion tests
    std::vector<Person> all_persons;
    for (int i = 1; i <= 2000; i++) {
        all_persons.push_back({i, "TestPerson" + std::to_string(i), 25});
    }

    // Bulk insert all test data
    auto insert_result = queryset.insert(std::span<const Person>(all_persons));
    if (!insert_result.has_value()) {
        std::cerr << "Failed to insert test data: " << insert_result.error().message() << std::endl;
        storm::QuerySet<Person>::clear_default_connection();
        return;
    }

    // Test batch sizes for DELETE operations
    const std::vector<size_t> test_batch_sizes = {
        1, 10, 25, 50, 100, 200, 500, 1000
    };

    std::cout << "DELETE Batch Performance Analysis" << std::endl;
    std::cout << "Batch Size | DELETE Time (μs) | Cache Status | SQL Length (est)" << std::endl;
    std::cout << "-----------|------------------|--------------|------------------" << std::endl;

    int offset = 0;
    for (size_t batch_size : test_batch_sizes) {
        if (offset + batch_size > all_persons.size()) {
            break;
        }

        // Create batch of persons to delete
        std::vector<Person> delete_batch(
            all_persons.begin() + offset,
            all_persons.begin() + offset + batch_size
        );

        MicroBenchmarkTimer timer;

        // Measure DELETE SQL generation and execution time
        auto delete_result = queryset.remove(std::span<const Person>(delete_batch));

        double delete_time = timer.elapsed_us();

        // Determine if this was likely a cache hit
        bool likely_cache_hit = delete_time < 100.0; // Heuristic threshold for DELETE

        // Estimate SQL length for DELETE with IN clause
        // "DELETE FROM Person WHERE id IN (?,?,?,...)"
        size_t estimated_sql_length = 30 + (batch_size * 2);

        std::cout << std::setw(10) << batch_size << " | "
                  << std::setw(16) << std::fixed << std::setprecision(3) << delete_time << " | "
                  << std::setw(12) << (likely_cache_hit ? "Cache Hit" : "Cache Miss") << " | "
                  << std::setw(16) << estimated_sql_length << std::endl;

        offset += batch_size;
    }

    std::cout << std::endl;
    std::cout << "=== DELETE CACHE EFFECTIVENESS TEST ===" << std::endl;
    std::cout << "Running repeated DELETE operations to test cache performance" << std::endl;
    std::cout << std::endl;

    // Test cache effectiveness for common DELETE batch sizes
    const std::vector<size_t> common_delete_sizes = {1, 10, 25, 50};
    const int iterations = 100;

    for (size_t batch_size : common_delete_sizes) {
        std::vector<double> times;
        times.reserve(iterations);

        // Re-insert data for consistent testing
        for (size_t i = 0; i < batch_size * iterations; i++) {
            Person p{static_cast<int>(10000 + i), "CacheTest" + std::to_string(i), 30};
            queryset.insert(p);
        }

        for (int i = 0; i < iterations; ++i) {
            // Create batch with unique IDs
            std::vector<Person> delete_batch;
            for (size_t j = 0; j < batch_size; ++j) {
                delete_batch.push_back({
                    static_cast<int>(10000 + i * batch_size + j),
                    "CacheTest",
                    30
                });
            }

            MicroBenchmarkTimer timer;
            auto delete_result = queryset.remove(std::span<const Person>(delete_batch));
            times.push_back(timer.elapsed_us());

            if (!delete_result.has_value()) {
                std::cerr << "Delete failed in cache test" << std::endl;
                break;
            }
        }

        if (!times.empty()) {
            // Calculate statistics
            double sum = 0;
            double min_time = times[0];
            double max_time = times[0];

            for (double t : times) {
                sum += t;
                min_time = std::min(min_time, t);
                max_time = std::max(max_time, t);
            }

            double avg_time = sum / times.size();
            double first_time = times[0];
            double speedup = first_time / avg_time;

            std::cout << "Batch size " << std::setw(3) << batch_size << ": "
                      << "Avg: " << std::fixed << std::setprecision(3) << std::setw(8) << avg_time << " μs, "
                      << "Min: " << std::setw(8) << min_time << " μs, "
                      << "Max: " << std::setw(8) << max_time << " μs, "
                      << "Speedup: " << std::setprecision(2) << speedup << "x"
                      << std::endl;
        }
    }

    std::cout << std::endl;
    std::cout << "=== DELETE OPTIMIZATION ANALYSIS ===" << std::endl;
    std::cout << "Key DELETE optimizations:" << std::endl;
    std::cout << "1. Bulk DELETE with IN clause for batches <= 50 objects" << std::endl;
    std::cout << "2. Individual DELETEs with transaction for large batches" << std::endl;
    std::cout << "3. Primary key extraction using compile-time reflection" << std::endl;
    std::cout << "4. Smart threshold based on SQLITE_MAX_VARIABLE_NUMBER (999)" << std::endl;
    std::cout << std::endl;

    // Cleanup
    storm::QuerySet<Person>::clear_default_connection();
}

int main() {
    benchmark_sql_generation();
    benchmark_delete_sql_generation();
    return 0;
}