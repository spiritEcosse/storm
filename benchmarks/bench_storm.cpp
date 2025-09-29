#include <chrono>
#include <iostream>
#include <vector>
#include <iomanip>
#include <string>

import storm;
import <expected>;
import <span>;

// Forward declarations
void benchmark_storm_orm_single_insert(int num_records);
void benchmark_storm_orm_batch_insert(int num_records);
void benchmark_storm_orm_single_delete(int num_records);
void benchmark_storm_orm_batch_delete(int num_records);

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

void benchmark_storm_orm_single_insert(int num_records) {
    std::cout << "=== Storm ORM Single INSERT Benchmark ===\n";

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
    for (int i = 1; i <= num_records; ++i) {
        persons.push_back({i, "Person" + std::to_string(i), 20 + (i % 50)});
    }

    // Create QuerySet
    auto queryset = storm::QuerySet<Person>{};

    // Benchmark single INSERT operations
    BenchmarkTimer timer;
    double total_time = 0;
    int successful_inserts = 0;

    for (const auto& person : persons) {
        timer.reset();
        auto result = queryset.insert(person);
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            successful_inserts++;
            total_time += elapsed;
        }
    }

    // Report results
    std::cout << "Storm ORM - Single INSERT " << num_records << " records:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
    std::cout << "  Average per insert: " << std::fixed << std::setprecision(4)
              << (total_time / successful_inserts) << " ms\n";
    std::cout << "  Successful inserts: " << successful_inserts << "/" << num_records << "\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (successful_inserts / (total_time / 1000.0)) << " inserts/sec\n";

    // Cleanup
    storm::QuerySet<Person>::clear_default_connection();
}

void benchmark_storm_orm_batch_insert(int num_records) {
    std::cout << "=== Storm ORM Batch INSERT Benchmark ===\n";

    // Test different batch sizes to find optimal performance
    const std::vector<size_t> batch_sizes = {1, 10, 25, 50, 100, 500, 1000};

    for (size_t batch_size : batch_sizes) {
        if (batch_size > static_cast<size_t>(num_records)) continue;

        std::cout << "\n--- Batch size: " << batch_size << " ---\n";

        // Setup Storm ORM connection
        auto result = storm::QuerySet<Person>::set_default_connection(":memory:");
        if (!result.has_value()) {
            std::cerr << "Failed to set Storm connection: " << result.error().message() << std::endl;
            continue;
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
            storm::QuerySet<Person>::clear_default_connection();
            continue;
        }

        // Prepare data
        std::vector<Person> persons;
        persons.reserve(num_records);
        for (int i = 1; i <= num_records; ++i) {
            persons.push_back({i, "Person" + std::to_string(i), 20 + (i % 50)});
        }

        // Create QuerySet
        auto queryset = storm::QuerySet<Person>{};

        // Benchmark batch INSERT operations
        BenchmarkTimer timer;
        double total_time = 0;
        int successful_inserts = 0;
        int batch_count = 0;

        for (size_t i = 0; i < persons.size(); i += batch_size) {
            size_t end_idx = std::min(i + batch_size, persons.size());
            std::span<const Person> batch(persons.data() + i, end_idx - i);

            timer.reset();
            auto result = queryset.insert(batch);
            double elapsed = timer.elapsed_ms();

            if (result.has_value()) {
                successful_inserts += batch.size();
                total_time += elapsed;
                batch_count++;
            }
        }

        // Report results
        std::cout << "Storm ORM - Batch INSERT " << num_records << " records (batch size " << batch_size << "):\n";
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
        std::cout << "  Average per insert: " << std::fixed << std::setprecision(4)
                  << (total_time / successful_inserts) << " ms\n";
        std::cout << "  Average per batch: " << std::fixed << std::setprecision(4)
                  << (total_time / batch_count) << " ms\n";
        std::cout << "  Successful inserts: " << successful_inserts << "/" << num_records << "\n";
        std::cout << "  Batch count: " << batch_count << "\n";
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
                  << (successful_inserts / (total_time / 1000.0)) << " inserts/sec\n";

        // Cleanup
        storm::QuerySet<Person>::clear_default_connection();
    }
}

int main() {
    std::cout << "=== Storm ORM INSERT/DELETE Benchmark ===\n\n";

    const std::vector<int> test_sizes = {1000, 5000, 10000};

    for (int size : test_sizes) {
        std::cout << "========================================\n";
        std::cout << "Testing with " << size << " records\n";
        std::cout << "========================================\n\n";

        // Test single INSERT operations
        benchmark_storm_orm_single_insert(size);
        std::cout << "\n\n";

        // Test batch INSERT operations with different batch sizes
        benchmark_storm_orm_batch_insert(size);
        std::cout << "\n\n";

        // Test single DELETE operations
        benchmark_storm_orm_single_delete(size);
        std::cout << "\n\n";

        // Test batch DELETE operations with different batch sizes
        benchmark_storm_orm_batch_delete(size);
        std::cout << "\n\n";
    }

    return 0;
}

void benchmark_storm_orm_single_delete(int num_records) {
    std::cout << "=== Storm ORM Single DELETE Benchmark ===\n";

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
    for (int i = 1; i <= num_records; ++i) {
        persons.push_back({i, "Person" + std::to_string(i), 20 + (i % 50)});
    }

    // Create QuerySet
    auto queryset = storm::QuerySet<Person>{};

    // Insert test data first for deletion
    for (const auto& person : persons) {
        auto insert_result = queryset.insert(person);
        if (!insert_result.has_value()) {
            std::cerr << "Failed to insert test data: " << insert_result.error().message() << std::endl;
            return;
        }
    }

    // Benchmark single DELETE operations
    BenchmarkTimer timer;
    double total_time = 0;
    int successful_deletes = 0;

    for (const auto& person : persons) {
        timer.reset();
        auto result = queryset.remove(person);
        double elapsed = timer.elapsed_ms();

        if (result.has_value() && result.value()) {
            successful_deletes++;
            total_time += elapsed;
        }
    }

    // Report results
    std::cout << "Storm ORM - Single DELETE " << num_records << " records:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
    std::cout << "  Average per delete: " << std::fixed << std::setprecision(4)
              << (total_time / successful_deletes) << " ms\n";
    std::cout << "  Successful deletes: " << successful_deletes << "/" << num_records << "\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (successful_deletes / (total_time / 1000.0)) << " deletes/sec\n";

    // Cleanup
    storm::QuerySet<Person>::clear_default_connection();
}

void benchmark_storm_orm_batch_delete(int num_records) {
    std::cout << "=== Storm ORM Batch DELETE Benchmark ===\n";

    // Test different batch sizes to find optimal performance
    const std::vector<size_t> batch_sizes = {1, 10, 25, 50, 100, 500, 1000};

    for (size_t batch_size : batch_sizes) {
        if (batch_size > static_cast<size_t>(num_records)) continue;

        std::cout << "\n--- Batch size: " << batch_size << " ---\n";

        // Setup Storm ORM connection
        auto result = storm::QuerySet<Person>::set_default_connection(":memory:");
        if (!result.has_value()) {
            std::cerr << "Failed to set Storm connection: " << result.error().message() << std::endl;
            continue;
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
            storm::QuerySet<Person>::clear_default_connection();
            continue;
        }

        // Prepare data
        std::vector<Person> persons;
        persons.reserve(num_records);
        for (int i = 1; i <= num_records; ++i) {
            persons.push_back({i, "Person" + std::to_string(i), 20 + (i % 50)});
        }

        // Create QuerySet
        auto queryset = storm::QuerySet<Person>{};

        // Insert test data first for deletion
        for (const auto& person : persons) {
            auto insert_result = queryset.insert(person);
            if (!insert_result.has_value()) {
                std::cerr << "Failed to insert test data: " << insert_result.error().message() << std::endl;
                storm::QuerySet<Person>::clear_default_connection();
                continue;
            }
        }

        // Benchmark batch DELETE operations
        BenchmarkTimer timer;
        double total_time = 0;
        int successful_deletes = 0;
        int batch_count = 0;

        for (size_t i = 0; i < persons.size(); i += batch_size) {
            size_t end_idx = std::min(i + batch_size, persons.size());
            size_t current_batch_size = end_idx - i;

            timer.reset();

            // Use individual removes in batches with timing per batch
            bool batch_success = true;
            for (size_t j = i; j < end_idx; ++j) {
                auto result = queryset.remove(persons[j]);
                if (!result.has_value() || !result.value()) {
                    batch_success = false;
                    break;
                }
            }

            double elapsed = timer.elapsed_ms();

            if (batch_success) {
                successful_deletes += current_batch_size;
                total_time += elapsed;
                batch_count++;
            }
        }

        // Report results
        std::cout << "Storm ORM - Batch DELETE " << num_records << " records (batch size " << batch_size << "):\n";
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
        std::cout << "  Average per delete: " << std::fixed << std::setprecision(4)
                  << (total_time / successful_deletes) << " ms\n";
        std::cout << "  Average per batch: " << std::fixed << std::setprecision(4)
                  << (total_time / batch_count) << " ms\n";
        std::cout << "  Successful deletes: " << successful_deletes << "/" << num_records << "\n";
        std::cout << "  Batch count: " << batch_count << "\n";
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
                  << (successful_deletes / (total_time / 1000.0)) << " deletes/sec\n";

        // Cleanup
        storm::QuerySet<Person>::clear_default_connection();
    }
}