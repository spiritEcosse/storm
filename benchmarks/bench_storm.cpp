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

void benchmark_storm_orm_remove(int num_records) {
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

    // Insert test data
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

    // Benchmark removal
    BenchmarkTimer timer;
    double total_time = 0;
    int successful_removes = 0;

    for (const auto& person : persons) {
        timer.reset();
        auto result = queryset.remove(person);
        double elapsed = timer.elapsed_ms();

        if (result.has_value() && result.value()) {
            successful_removes++;
            total_time += elapsed;
        }
    }

    // Report results
    std::cout << "Storm ORM - Remove " << num_records << " records:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
    std::cout << "  Average per remove: " << std::fixed << std::setprecision(4)
              << (total_time / successful_removes) << " ms\n";
    std::cout << "  Successful removes: " << successful_removes << "/" << num_records << "\n";

    // Cleanup
    storm::QuerySet<Person>::clear_default_connection();
}

int main() {
    std::cout << "=== Storm ORM Benchmark ===\n\n";

    const std::vector<int> test_sizes = {1000, 5000, 10000};

    for (int size : test_sizes) {
        std::cout << "--- Testing with " << size << " records ---\n";
        benchmark_storm_orm_remove(size);
        std::cout << "\n\n";
    }

    return 0;
}