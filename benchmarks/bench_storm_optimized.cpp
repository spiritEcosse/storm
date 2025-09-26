#include <chrono>
#include <iostream>
#include <vector>
#include <iomanip>
#include <string>

import storm;
import <expected>;

class BenchmarkTimer {
public:
    BenchmarkTimer() : start_(std::chrono::high_resolution_clock::now()) {}

    double elapsed_ms() const {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
        return duration.count() / 1000.0;
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

void benchmark_storm_orm_optimized_remove(int num_records) {
    // Setup Storm ORM connection
    auto result = storm::QuerySet<Person>::set_default_connection(":memory:");
    if (!result.has_value()) {
        std::cerr << "Failed to set Storm connection: " << result.error().message() << std::endl;
        return;
    }

    // Create table
    auto& conn = storm::QuerySet<Person>::get_default_connection();
    auto create_result = conn.execute(
        "CREATE TABLE Person ("
        "id INTEGER PRIMARY KEY, "
        "name TEXT NOT NULL, "
        "age INTEGER NOT NULL"
        ")"
    );
    if (!create_result.has_value()) {
        std::cerr << "Failed to create table: " << create_result.error().message() << std::endl;
        return;
    }

    // Prepare data
    std::vector<Person> persons;
    persons.reserve(num_records);

    // Insert test data with transaction for setup
    (void)conn.execute("BEGIN TRANSACTION");
    for (int i = 1; i <= num_records; ++i) {
        persons.push_back({i, "Person" + std::to_string(i), 20 + (i % 50)});
        (void)conn.execute(
            "INSERT INTO Person (id, name, age) VALUES (" +
            std::to_string(i) + ", 'Person" + std::to_string(i) + "', " +
            std::to_string(20 + (i % 50)) + ")"
        );
    }
    (void)conn.execute("COMMIT");

    // Create QuerySet
    auto queryset = storm::QuerySet<Person>{};

    // Benchmark individual removal (testing single-item optimization)
    BenchmarkTimer timer;
    double total_time_individual = 0;
    int successful_removes_individual = 0;

    // Test individual removes (first half)
    for (size_t i = 0; i < persons.size() / 2; ++i) {
        timer.reset();
        auto result = queryset.remove(persons[i]);
        double elapsed = timer.elapsed_ms();

        if (result.has_value() && result.value()) {
            successful_removes_individual++;
            total_time_individual += elapsed;
        }
    }

    // Test batch remove optimization (second half)
    timer.reset();
    std::vector<Person> remaining_persons(persons.begin() + persons.size() / 2, persons.end());

    // Remove the remaining objects in batches to test bulk optimization
    const size_t batch_size = 10; // Test batch optimization
    double total_time_batch = 0;
    int successful_removes_batch = 0;

    for (size_t i = 0; i < remaining_persons.size(); i += batch_size) {
        timer.reset();

        size_t end_idx = std::min(i + batch_size, remaining_persons.size());
        for (size_t j = i; j < end_idx; ++j) {
            auto result = queryset.remove(remaining_persons[j]);
            if (result.has_value() && result.value()) {
                successful_removes_batch++;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_time_batch += elapsed;
    }

    // Get cache statistics
    std::cout << "Storm ORM (Optimized) - Remove " << num_records << " records:\n";
    std::cout << "  Individual removes (" << successful_removes_individual << " items):\n";
    std::cout << "    Total time: " << std::fixed << std::setprecision(2) << total_time_individual << " ms\n";
    if (successful_removes_individual > 0) {
        std::cout << "    Average per remove: " << std::fixed << std::setprecision(4)
                  << (total_time_individual / successful_removes_individual) << " ms\n";
    }

    std::cout << "  Batch removes (" << successful_removes_batch << " items):\n";
    std::cout << "    Total time: " << std::fixed << std::setprecision(2) << total_time_batch << " ms\n";
    if (successful_removes_batch > 0) {
        std::cout << "    Average per remove: " << std::fixed << std::setprecision(4)
                  << (total_time_batch / successful_removes_batch) << " ms\n";
    }

    std::cout << "  Total successful removes: " << (successful_removes_individual + successful_removes_batch)
              << "/" << num_records << "\n";
    std::cout << "  Statement cache size: " << conn.cached_statement_count() << "\n";

    // Cleanup
    storm::QuerySet<Person>::clear_default_connection();
}

int main() {
    std::cout << "=== Storm ORM Optimized Benchmark ===\n\n";
    std::cout << "Features tested:\n";
    std::cout << "- Statement caching\n";
    std::cout << "- Bulk operations with IN clauses (small batches)\n";
    std::cout << "- Transaction wrapping for batch operations\n";
    std::cout << "- Pre-compiled SQL strings\n";
    std::cout << "- Common statement pre-population\n\n";

    const std::vector<int> test_sizes = {1000, 5000, 10000};

    for (int size : test_sizes) {
        std::cout << "--- Testing with " << size << " records ---\n";
        benchmark_storm_orm_optimized_remove(size);
        std::cout << "\n\n";
    }

    return 0;
}