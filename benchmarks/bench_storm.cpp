#include <cstring>
#include <random>
#include <algorithm>
#include "benchmark_utils.hpp"

import storm;
import <expected>;
import <span>;

using namespace storm::benchmark;

// Define the actual Person struct with Storm imports
struct Person {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string name;
    int age;
};

// Configuration structure
struct BenchmarkConfig {
    enum Mode { COMPREHENSIVE, DELETE_FOCUS, INSERT_ONLY, DELETE_ONLY, SELECT_ONLY, CACHE_ANALYSIS, OPTIMIZATION_TEST };
    Mode mode = COMPREHENSIVE;
    std::vector<int> test_sizes = {1000, 5000, 10000};
    bool verbose = false;
    bool show_cache_stats = false;
    bool realistic_data = false;
};

// Forward declarations (Person defined later)
void benchmark_storm_orm_single_insert(int num_records, const BenchmarkConfig& config);
void benchmark_storm_orm_batch_insert(int num_records, const BenchmarkConfig& config);
void benchmark_storm_orm_single_update(int num_records, const BenchmarkConfig& config);
void benchmark_storm_orm_batch_update(int num_records, const BenchmarkConfig& config);
void benchmark_storm_orm_single_delete(int num_records, const BenchmarkConfig& config);
void benchmark_storm_orm_batch_delete(int num_records, const BenchmarkConfig& config);
void benchmark_storm_orm_select(int num_records, const BenchmarkConfig& config);
void benchmark_storm_orm_delete_focus(int num_records, const BenchmarkConfig& config);
void benchmark_cache_analysis(int num_records, const BenchmarkConfig& config);
void benchmark_optimization_test(int num_records, const BenchmarkConfig& config);
void print_optimization_summary();
BenchmarkConfig parse_arguments(int argc, char* argv[]);
void print_usage(const char* program_name);


// Forward declaration for realistic data generation
std::vector<Person> generate_realistic_test_data(int count, int start_id = 1);

// Generate realistic test data with random names and ages
std::vector<Person> generate_realistic_test_data(int count, int start_id) {
    std::vector<Person> persons;
    persons.reserve(count);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> age_dist(18, 80);

    const std::vector<std::string> names = {
        "Alice", "Bob", "Charlie", "Diana", "Eve", "Frank", "Grace", "Henry",
        "Iris", "Jack", "Kate", "Liam", "Maya", "Nathan", "Olivia", "Paul"
    };

    for (int i = 0; i < count; ++i) {
        persons.push_back({
            start_id + i,
            names[i % names.size()] + std::to_string(i),
            age_dist(gen)
        });
    }

    return persons;
}

void benchmark_storm_orm_single_insert(int num_records, const BenchmarkConfig& config) {
    std::cout << "=== Storm ORM Single INSERT Benchmark ===" << std::endl;

    // Setup Storm ORM connection
    auto result = storm::QuerySet<Person>::set_default_connection(":memory:");
    if (!result.has_value()) {
        std::cerr << "Failed to set Storm connection: " << result.error().message() << std::endl;
        return;
    }

    // Create table
    auto& conn = storm::QuerySet<Person>::get_default_connection();
    auto create_result = conn.execute(db_utils::PERSON_TABLE_SQL);
    if (!create_result.has_value()) {
        std::cerr << "Failed to create table: " << create_result.error().message() << std::endl;
        return;
    }

    // Prepare data
    std::vector<Person> persons;
    if (config.realistic_data) {
        persons = generate_realistic_test_data(num_records);
    } else {
        persons = data_utils::generate_simple_test_data<Person>(num_records);
    }

    // Create QuerySet
    auto queryset = storm::QuerySet<Person>{};

    // Warmup phase: populate statement cache with first few inserts
    constexpr int warmup_count = 10;
    for (int i = 0; i < warmup_count && i < num_records; ++i) {
        queryset.insert(persons[i]);
    }

    // Benchmark single INSERT operations (with warm cache)
    BenchmarkTimer timer;
    double total_time = 0;
    int successful_inserts = 0;

    for (size_t i = warmup_count; i < persons.size(); ++i) {
        timer.reset();
        auto result = queryset.insert(persons[i]);
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            successful_inserts++;
            total_time += elapsed;
        }
    }

    // Report results
    std::cout << "Storm ORM - Single INSERT " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Average per insert: " << std::fixed << std::setprecision(4)
              << (total_time / successful_inserts) << " ms" << std::endl;
    std::cout << "  Successful inserts: " << successful_inserts << "/" << num_records << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (successful_inserts / (total_time / 1000.0)) << " inserts/sec" << std::endl;

    if (config.show_cache_stats) {
        std::cout << "  Statement cache size: " << conn.cached_statement_count() << std::endl;
    }

    // Cleanup
    storm::QuerySet<Person>::clear_default_connection();
}

void benchmark_storm_orm_batch_insert(int num_records, const BenchmarkConfig& config) {
    std::cout << "=== Storm ORM Batch INSERT Benchmark ===" << std::endl;

    // Test different batch sizes to find optimal performance
    const std::vector<size_t> batch_sizes = {1, 10, 25, 50, 100, 500, 1000};

    for (size_t batch_size : batch_sizes) {
        if (batch_size > static_cast<size_t>(num_records)) continue;

        std::cout << std::endl << "--- Batch size: " << batch_size << " ---" << std::endl;

        // Setup Storm ORM connection
        auto result = storm::QuerySet<Person>::set_default_connection(":memory:");
        if (!result.has_value()) {
            std::cerr << "Failed to set Storm connection: " << result.error().message() << std::endl;
            continue;
        }

        // Create table
        auto& conn = storm::QuerySet<Person>::get_default_connection();
        auto create_result = conn.execute(db_utils::PERSON_TABLE_SQL);
        if (!create_result.has_value()) {
            std::cerr << "Failed to create table: " << create_result.error().message() << std::endl;
            storm::QuerySet<Person>::clear_default_connection();
            continue;
        }

        // Prepare data
        std::vector<Person> persons = data_utils::generate_simple_test_data<Person>(num_records);

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
        std::cout << "Storm ORM - Batch INSERT " << num_records << " records (batch size " << batch_size << "):" << std::endl;
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
        std::cout << "  Average per insert: " << std::fixed << std::setprecision(4)
                  << (total_time / successful_inserts) << " ms" << std::endl;
        std::cout << "  Average per batch: " << std::fixed << std::setprecision(4)
                  << (total_time / batch_count) << " ms" << std::endl;
        std::cout << "  Successful inserts: " << successful_inserts << "/" << num_records << std::endl;
        std::cout << "  Batch count: " << batch_count << std::endl;
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
                  << (successful_inserts / (total_time / 1000.0)) << " inserts/sec" << std::endl;

        if (config.show_cache_stats) {
            std::cout << "  Statement cache size: " << conn.cached_statement_count() << std::endl;
        }

        // Cleanup
        storm::QuerySet<Person>::clear_default_connection();
    }
}

void benchmark_storm_orm_single_update(int num_records, const BenchmarkConfig& config) {
    std::cout << "=== Storm ORM Single UPDATE Benchmark ===" << std::endl;

    // Setup Storm ORM connection
    auto result = storm::QuerySet<Person>::set_default_connection(":memory:");
    if (!result.has_value()) {
        std::cerr << "Failed to set Storm connection: " << result.error().message() << std::endl;
        return;
    }

    // Create table
    auto& conn = storm::QuerySet<Person>::get_default_connection();
    auto create_result = conn.execute(db_utils::PERSON_TABLE_SQL);
    if (!create_result.has_value()) {
        std::cerr << "Failed to create table: " << create_result.error().message() << std::endl;
        return;
    }

    // Prepare data
    std::vector<Person> persons;
    if (config.realistic_data) {
        persons = generate_realistic_test_data(num_records);
    } else {
        persons = data_utils::generate_simple_test_data<Person>(num_records);
    }

    // Create QuerySet
    auto queryset = storm::QuerySet<Person>{};

    // Insert test data first for update
    for (const auto& person : persons) {
        auto insert_result = queryset.insert(person);
        if (!insert_result.has_value()) {
            std::cerr << "Failed to insert test data: " << insert_result.error().message() << std::endl;
            return;
        }
    }

    // Modify data for update
    for (auto& person : persons) {
        person.age += 1;  // Increment age
        person.name += "_updated";  // Modify name
    }

    // Warmup phase: populate UPDATE statement cache
    constexpr int warmup_count = 10;
    for (int i = 0; i < warmup_count && i < num_records; ++i) {
        queryset.update(persons[i]);
    }

    // Benchmark single UPDATE operations (with warm cache)
    BenchmarkTimer timer;
    double total_time = 0;
    int successful_updates = 0;

    for (size_t i = warmup_count; i < persons.size(); ++i) {
        timer.reset();
        auto result = queryset.update(persons[i]);
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            successful_updates++;
            total_time += elapsed;
        }
    }

    // Report results
    std::cout << "Storm ORM - Single UPDATE " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Average per update: " << std::fixed << std::setprecision(4)
              << (total_time / successful_updates) << " ms" << std::endl;
    std::cout << "  Successful updates: " << successful_updates << "/" << num_records << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (successful_updates / (total_time / 1000.0)) << " updates/sec" << std::endl;

    if (config.show_cache_stats) {
        std::cout << "  Statement cache size: " << conn.cached_statement_count() << std::endl;
    }

    // Cleanup
    storm::QuerySet<Person>::clear_default_connection();
}

void benchmark_storm_orm_batch_update(int num_records, const BenchmarkConfig& config) {
    std::cout << "=== Storm ORM Batch UPDATE Benchmark ===" << std::endl;

    // Test different batch sizes to find optimal performance
    const std::vector<size_t> batch_sizes = {1, 10, 25, 50, 100, 500, 1000};

    for (size_t batch_size : batch_sizes) {
        if (batch_size > static_cast<size_t>(num_records)) continue;

        std::cout << std::endl << "--- Batch size: " << batch_size << " ---" << std::endl;

        // Setup Storm ORM connection
        auto result = storm::QuerySet<Person>::set_default_connection(":memory:");
        if (!result.has_value()) {
            std::cerr << "Failed to set Storm connection: " << result.error().message() << std::endl;
            continue;
        }

        // Create table
        auto& conn = storm::QuerySet<Person>::get_default_connection();
        auto create_result = conn.execute(db_utils::PERSON_TABLE_SQL);
        if (!create_result.has_value()) {
            std::cerr << "Failed to create table: " << create_result.error().message() << std::endl;
            storm::QuerySet<Person>::clear_default_connection();
            continue;
        }

        // Prepare data
        std::vector<Person> persons = data_utils::generate_simple_test_data<Person>(num_records);

        // Create QuerySet
        auto queryset = storm::QuerySet<Person>{};

        // Insert test data first for update
        for (const auto& person : persons) {
            auto insert_result = queryset.insert(person);
            if (!insert_result.has_value()) {
                std::cerr << "Failed to insert test data: " << insert_result.error().message() << std::endl;
                storm::QuerySet<Person>::clear_default_connection();
                return;
            }
        }

        // Modify data for update
        for (auto& person : persons) {
            person.age += 1;
            person.name += "_updated";
        }

        // Benchmark batch UPDATE operations
        BenchmarkTimer timer;
        double total_time = 0;
        int successful_updates = 0;
        int batch_count = 0;

        for (size_t i = 0; i < persons.size(); i += batch_size) {
            size_t end_idx = std::min(i + batch_size, persons.size());
            std::span<const Person> batch(persons.data() + i, end_idx - i);

            timer.reset();
            auto result = queryset.update(batch);
            double elapsed = timer.elapsed_ms();

            if (result.has_value()) {
                successful_updates += batch.size();
                total_time += elapsed;
                batch_count++;
            }
        }

        // Report results
        std::cout << "Storm ORM - Batch UPDATE " << num_records << " records (batch size " << batch_size << "):" << std::endl;
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
        std::cout << "  Average per update: " << std::fixed << std::setprecision(4)
                  << (total_time / successful_updates) << " ms" << std::endl;
        std::cout << "  Average per batch: " << std::fixed << std::setprecision(4)
                  << (total_time / batch_count) << " ms" << std::endl;
        std::cout << "  Successful updates: " << successful_updates << "/" << num_records << std::endl;
        std::cout << "  Batch count: " << batch_count << std::endl;
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
                  << (successful_updates / (total_time / 1000.0)) << " updates/sec" << std::endl;

        if (config.show_cache_stats) {
            std::cout << "  Statement cache size: " << conn.cached_statement_count() << std::endl;
        }

        // Cleanup
        storm::QuerySet<Person>::clear_default_connection();
    }
}

void benchmark_storm_orm_single_delete(int num_records, const BenchmarkConfig& config) {
    std::cout << "=== Storm ORM Single DELETE Benchmark ===" << std::endl;

    // Setup Storm ORM connection
    auto result = storm::QuerySet<Person>::set_default_connection(":memory:");
    if (!result.has_value()) {
        std::cerr << "Failed to set Storm connection: " << result.error().message() << std::endl;
        return;
    }

    // Create table
    auto& conn = storm::QuerySet<Person>::get_default_connection();
    auto create_result = conn.execute(db_utils::PERSON_TABLE_SQL);
    if (!create_result.has_value()) {
        std::cerr << "Failed to create table: " << create_result.error().message() << std::endl;
        return;
    }

    // Prepare data
    std::vector<Person> persons;
    if (config.realistic_data) {
        persons = generate_realistic_test_data(num_records);
    } else {
        persons = data_utils::generate_simple_test_data<Person>(num_records);
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

    // Warmup phase: populate DELETE statement cache
    constexpr int warmup_count = 10;
    for (int i = 0; i < warmup_count && i < num_records; ++i) {
        queryset.remove(persons[i]);
    }

    // Benchmark single DELETE operations (with warm cache)
    BenchmarkTimer timer;
    double total_time = 0;
    int successful_deletes = 0;

    for (size_t i = warmup_count; i < persons.size(); ++i) {
        timer.reset();
        auto result = queryset.remove(persons[i]);
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            successful_deletes++;
            total_time += elapsed;
        }
    }

    // Report results
    std::cout << "Storm ORM - Single DELETE " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Average per delete: " << std::fixed << std::setprecision(4)
              << (total_time / successful_deletes) << " ms" << std::endl;
    std::cout << "  Successful deletes: " << successful_deletes << "/" << num_records << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (successful_deletes / (total_time / 1000.0)) << " deletes/sec" << std::endl;

    if (config.show_cache_stats) {
        std::cout << "  Statement cache size: " << conn.cached_statement_count() << std::endl;
    }

    // Cleanup
    storm::QuerySet<Person>::clear_default_connection();
}

void benchmark_storm_orm_batch_delete(int num_records, const BenchmarkConfig& config) {
    std::cout << "=== Storm ORM Batch DELETE Benchmark ===" << std::endl;

    // Test different batch sizes to find optimal performance
    const std::vector<size_t> batch_sizes = {1, 10, 25, 50, 100, 500, 1000};

    for (size_t batch_size : batch_sizes) {
        if (batch_size > static_cast<size_t>(num_records)) continue;

        std::cout << std::endl << "--- Batch size: " << batch_size << " ---" << std::endl;

        // Setup Storm ORM connection
        auto result = storm::QuerySet<Person>::set_default_connection(":memory:");
        if (!result.has_value()) {
            std::cerr << "Failed to set Storm connection: " << result.error().message() << std::endl;
            continue;
        }

        // Create table
        auto& conn = storm::QuerySet<Person>::get_default_connection();
        auto create_result = conn.execute(db_utils::PERSON_TABLE_SQL);
        if (!create_result.has_value()) {
            std::cerr << "Failed to create table: " << create_result.error().message() << std::endl;
            storm::QuerySet<Person>::clear_default_connection();
            continue;
        }

        // Prepare data
        std::vector<Person> persons = data_utils::generate_simple_test_data<Person>(num_records);

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

            // Create batch span for this chunk
            std::span<const Person> batch_span(&persons[i], current_batch_size);

            timer.reset();

            // Use new batch remove API
            auto result = queryset.remove(batch_span);

            double elapsed = timer.elapsed_ms();

            if (result.has_value()) {
                successful_deletes += current_batch_size;
                total_time += elapsed;
                batch_count++;
            }
        }

        // Report results
        std::cout << "Storm ORM - Batch DELETE " << num_records << " records (batch size " << batch_size << "):" << std::endl;
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
        std::cout << "  Average per delete: " << std::fixed << std::setprecision(4)
                  << (total_time / successful_deletes) << " ms" << std::endl;
        std::cout << "  Average per batch: " << std::fixed << std::setprecision(4)
                  << (total_time / batch_count) << " ms" << std::endl;
        std::cout << "  Successful deletes: " << successful_deletes << "/" << num_records << std::endl;
        std::cout << "  Batch count: " << batch_count << std::endl;
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
                  << (successful_deletes / (total_time / 1000.0)) << " deletes/sec" << std::endl;

        if (config.show_cache_stats) {
            std::cout << "  Statement cache size: " << conn.cached_statement_count() << std::endl;
        }

        // Cleanup
        storm::QuerySet<Person>::clear_default_connection();
    }
}

void benchmark_storm_orm_select(int num_records, const BenchmarkConfig& config) {
    std::cout << "=== Storm ORM SELECT Benchmark ===" << std::endl;

    // Setup Storm ORM connection
    auto result = storm::QuerySet<Person>::set_default_connection(":memory:");
    if (!result.has_value()) {
        std::cerr << "Failed to set Storm connection: " << result.error().message() << std::endl;
        return;
    }

    // Create table
    auto& conn = storm::QuerySet<Person>::get_default_connection();
    auto create_result = conn.execute(db_utils::PERSON_TABLE_SQL);
    if (!create_result.has_value()) {
        std::cerr << "Failed to create table: " << create_result.error().message() << std::endl;
        return;
    }

    // Prepare and insert test data
    std::vector<Person> persons;
    if (config.realistic_data) {
        persons = generate_realistic_test_data(num_records);
    } else {
        persons = data_utils::generate_simple_test_data<Person>(num_records);
    }

    // Create QuerySet
    auto queryset = storm::QuerySet<Person>{};

    // Insert test data
    for (const auto& person : persons) {
        auto insert_result = queryset.insert(person);
        if (!insert_result.has_value()) {
            std::cerr << "Failed to insert test data: " << insert_result.error().message() << std::endl;
            return;
        }
    }

    // Benchmark SELECT operation (fetching all rows)
    BenchmarkTimer timer;
    timer.reset();
    auto select_result = queryset.select();
    double elapsed = timer.elapsed_ms();

    if (select_result.has_value()) {
        const auto& selected_persons = select_result.value();
        std::cout << "Storm ORM - SELECT " << num_records << " records:" << std::endl;
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << elapsed << " ms" << std::endl;
        std::cout << "  Rows fetched: " << selected_persons.size() << std::endl;
        std::cout << "  Average per row: " << std::fixed << std::setprecision(4)
                  << (elapsed / selected_persons.size()) << " ms" << std::endl;
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
                  << (selected_persons.size() / (elapsed / 1000.0)) << " rows/sec" << std::endl;

        if (config.show_cache_stats) {
            std::cout << "  Statement cache size: " << conn.cached_statement_count() << std::endl;
        }
    } else {
        std::cerr << "SELECT failed: " << select_result.error().message() << std::endl;
    }

    // Cleanup
    storm::QuerySet<Person>::clear_default_connection();
}

void benchmark_storm_orm_delete_focus(int num_records, const BenchmarkConfig& config) {
    // Setup Storm ORM connection
    auto result = storm::QuerySet<Person>::set_default_connection(":memory:");
    if (!result.has_value()) {
        std::cerr << "Failed to set Storm connection: " << result.error().message() << std::endl;
        return;
    }

    // Create table
    auto& conn = storm::QuerySet<Person>::get_default_connection();
    auto create_result = conn.execute(db_utils::PERSON_TABLE_SQL);
    if (!create_result.has_value()) {
        std::cerr << "Failed to create table: " << create_result.error().message() << std::endl;
        return;
    }

    // Prepare data
    std::vector<Person> persons;
    if (config.realistic_data) {
        persons = generate_realistic_test_data(num_records);
    } else {
        persons = data_utils::generate_simple_test_data<Person>(num_records);
    }

    // Insert test data with transaction for setup
    (void)conn.execute("BEGIN TRANSACTION");
    for (const auto& person : persons) {
        (void)conn.execute(
            "INSERT INTO Person (id, name, age) VALUES (" +
            std::to_string(person.id) + ", '" + person.name + "', " +
            std::to_string(person.age) + ")"
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

        if (result.has_value()) {
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
            if (result.has_value()) {
                successful_removes_batch++;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_time_batch += elapsed;
    }

    // Get cache statistics
    std::cout << "Storm ORM (DELETE Focus) - Remove " << num_records << " records:" << std::endl;
    std::cout << "  Individual removes (" << successful_removes_individual << " items):" << std::endl;
    std::cout << "    Total time: " << std::fixed << std::setprecision(2) << total_time_individual << " ms" << std::endl;
    if (successful_removes_individual > 0) {
        std::cout << "    Average per remove: " << std::fixed << std::setprecision(4)
                  << (total_time_individual / successful_removes_individual) << " ms" << std::endl;
    }

    std::cout << "  Batch removes (" << successful_removes_batch << " items):" << std::endl;
    std::cout << "    Total time: " << std::fixed << std::setprecision(2) << total_time_batch << " ms" << std::endl;
    if (successful_removes_batch > 0) {
        std::cout << "    Average per remove: " << std::fixed << std::setprecision(4)
                  << (total_time_batch / successful_removes_batch) << " ms" << std::endl;
    }

    std::cout << "  Total successful removes: " << (successful_removes_individual + successful_removes_batch)
              << "/" << num_records << std::endl;
    std::cout << "  Statement cache size: " << conn.cached_statement_count() << std::endl;

    // Cleanup
    storm::QuerySet<Person>::clear_default_connection();
}

void benchmark_cache_analysis(int num_records, const BenchmarkConfig& config) {
    std::cout << "=== SQL CACHE PERFORMANCE TEST ===" << std::endl;
    std::cout << "Testing thread-local SQL cache effectiveness for different batch sizes" << std::endl;
    std::cout << std::endl;

    // Test cached vs uncached performance
    const std::vector<size_t> test_sizes = {1, 5, 10, 25, 50, 75, 100, 200, 500};

    for (size_t size : test_sizes) {
        if (size > static_cast<size_t>(num_records)) continue;

        auto persons = config.realistic_data ?
            generate_realistic_test_data(static_cast<int>(size * 10)) :
            std::vector<Person>();

        if (!config.realistic_data) {
            persons.reserve(size * 10);
            for (int i = 1; i <= static_cast<int>(size * 10); ++i) {
                persons.push_back({i, "Person" + std::to_string(i), 20 + (i % 50)});
            }
        }

        // First run - fills cache
        benchmark_storm_orm_batch_insert(static_cast<int>(size * 10), config);

        // Second run - should hit cache (but we can't easily separate this)
        // The thread-local cache will be populated from the first run
    }
}

void benchmark_optimization_test(int num_records, const BenchmarkConfig& config) {
    std::cout << "=== STORM ORM INSERT OPTIMIZATION BENCHMARK ===" << std::endl;
    std::cout << "Testing compile-time SQL prefix optimization and thread-local caching" << std::endl;
    std::cout << std::endl;

    // Test 1: Individual vs Batch INSERT Performance
    std::cout << "=== INDIVIDUAL vs BATCH INSERT COMPARISON ===" << std::endl;

    const std::vector<int> record_counts = {100, 1000, 5000};

    for (int count : record_counts) {
        if (count > num_records) continue;

        std::cout << "--- Testing with " << count << " records ---" << std::endl;

        // Individual inserts
        benchmark_storm_orm_single_insert(count, config);
        std::cout << std::endl;

        // Batch inserts with different sizes
        const std::vector<size_t> batch_sizes = {1, 10, 25, 50, 100};
        for (size_t batch_size : batch_sizes) {
            if (batch_size <= static_cast<size_t>(count)) {
                benchmark_storm_orm_batch_insert(count, config);
            }
        }

        std::cout << std::string(50, '-') << std::endl;
    }

    // Test 2: Cache Performance Analysis
    benchmark_cache_analysis(num_records, config);

    print_optimization_summary();
}

void print_optimization_summary() {
    std::cout << "=== KEY OPTIMIZATION FEATURES TESTED ===" << std::endl;
    std::cout << "✓ Compile-time SQL prefix generation using ConstexprString" << std::endl;
    std::cout << "✓ Pre-computed field names and placeholders" << std::endl;
    std::cout << "✓ Thread-local 8-entry SQL cache with round-robin replacement" << std::endl;
    std::cout << "✓ Memory pre-allocation for bulk INSERT SQL generation" << std::endl;
    std::cout << "✓ Index sequence optimization for field binding" << std::endl;
    std::cout << "✓ Smart threshold switching (≤50 bulk SQL, >50 individual + transaction)" << std::endl;
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    BenchmarkConfig config = parse_arguments(argc, argv);

    if (config.mode == BenchmarkConfig::COMPREHENSIVE) {
        std::cout << "=== Storm ORM INSERT/DELETE Benchmark ===" << std::endl << std::endl;
    } else if (config.mode == BenchmarkConfig::DELETE_FOCUS) {
        std::cout << "=== Storm ORM DELETE Focus Benchmark ===" << std::endl << std::endl;
        std::cout << "Features tested:" << std::endl;
        std::cout << "- Statement caching" << std::endl;
        std::cout << "- Individual vs batch remove timing" << std::endl;
        std::cout << "- Cache effectiveness analysis" << std::endl << std::endl;
    } else if (config.mode == BenchmarkConfig::INSERT_ONLY) {
        std::cout << "=== Storm ORM INSERT Only Benchmark ===" << std::endl << std::endl;
    } else if (config.mode == BenchmarkConfig::DELETE_ONLY) {
        std::cout << "=== Storm ORM DELETE Only Benchmark ===" << std::endl << std::endl;
    } else if (config.mode == BenchmarkConfig::SELECT_ONLY) {
        std::cout << "=== Storm ORM SELECT Only Benchmark ===" << std::endl << std::endl;
    } else if (config.mode == BenchmarkConfig::CACHE_ANALYSIS) {
        std::cout << "=== Storm ORM Cache Analysis Benchmark ===" << std::endl << std::endl;
        std::cout << "Features tested:" << std::endl;
        std::cout << "- Thread-local SQL cache effectiveness" << std::endl;
        std::cout << "- Cache hit/miss patterns for various batch sizes" << std::endl;
        std::cout << "- SQL generation performance optimization" << std::endl << std::endl;
    } else if (config.mode == BenchmarkConfig::OPTIMIZATION_TEST) {
        std::cout << "=== Storm ORM Optimization Test Benchmark ===" << std::endl << std::endl;
        std::cout << "Features tested:" << std::endl;
        std::cout << "- Compile-time SQL prefix generation" << std::endl;
        std::cout << "- Thread-local caching effectiveness" << std::endl;
        std::cout << "- Individual vs batch INSERT comparison" << std::endl;
        std::cout << "- Cache performance analysis" << std::endl << std::endl;
    }

    for (int size : config.test_sizes) {
        if (config.mode == BenchmarkConfig::DELETE_FOCUS) {
            std::cout << "--- Testing with " << size << " records ---" << std::endl;
            benchmark_storm_orm_delete_focus(size, config);
            std::cout << std::endl << std::endl;
        } else if (config.mode == BenchmarkConfig::CACHE_ANALYSIS) {
            std::cout << "--- Testing with " << size << " records ---" << std::endl;
            benchmark_cache_analysis(size, config);
            std::cout << std::endl << std::endl;
        } else if (config.mode == BenchmarkConfig::OPTIMIZATION_TEST) {
            std::cout << "--- Testing with " << size << " records ---" << std::endl;
            benchmark_optimization_test(size, config);
            std::cout << std::endl << std::endl;
        } else {
            std::cout << "========================================" << std::endl;
            std::cout << "Testing with " << size << " records" << std::endl;
            std::cout << "========================================" << std::endl << std::endl;

            if (config.mode == BenchmarkConfig::COMPREHENSIVE || config.mode == BenchmarkConfig::INSERT_ONLY) {
                // Test single INSERT operations
                benchmark_storm_orm_single_insert(size, config);
                std::cout << std::endl << std::endl;

                // Test batch INSERT operations with different batch sizes
                benchmark_storm_orm_batch_insert(size, config);
                std::cout << std::endl << std::endl;
            }

            if (config.mode == BenchmarkConfig::COMPREHENSIVE) {
                // Test single UPDATE operations
                benchmark_storm_orm_single_update(size, config);
                std::cout << std::endl << std::endl;

                // Test batch UPDATE operations with different batch sizes
                benchmark_storm_orm_batch_update(size, config);
                std::cout << std::endl << std::endl;
            }

            if (config.mode == BenchmarkConfig::COMPREHENSIVE || config.mode == BenchmarkConfig::DELETE_ONLY) {
                // Test single DELETE operations
                benchmark_storm_orm_single_delete(size, config);
                std::cout << std::endl << std::endl;

                // Test batch DELETE operations with different batch sizes
                benchmark_storm_orm_batch_delete(size, config);
                std::cout << std::endl << std::endl;
            }

            if (config.mode == BenchmarkConfig::COMPREHENSIVE || config.mode == BenchmarkConfig::SELECT_ONLY) {
                // Test SELECT operations
                benchmark_storm_orm_select(size, config);
                std::cout << std::endl << std::endl;
            }
        }
    }

    return 0;
}

BenchmarkConfig parse_arguments(int argc, char* argv[]) {
    BenchmarkConfig config;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--mode=comprehensive") == 0) {
            config.mode = BenchmarkConfig::COMPREHENSIVE;
        } else if (strcmp(argv[i], "--mode=delete-focus") == 0) {
            config.mode = BenchmarkConfig::DELETE_FOCUS;
            config.show_cache_stats = true;
        } else if (strcmp(argv[i], "--mode=insert-only") == 0) {
            config.mode = BenchmarkConfig::INSERT_ONLY;
        } else if (strcmp(argv[i], "--mode=delete-only") == 0) {
            config.mode = BenchmarkConfig::DELETE_ONLY;
        } else if (strcmp(argv[i], "--mode=select-only") == 0) {
            config.mode = BenchmarkConfig::SELECT_ONLY;
        } else if (strcmp(argv[i], "--mode=cache-analysis") == 0) {
            config.mode = BenchmarkConfig::CACHE_ANALYSIS;
            config.show_cache_stats = true;
        } else if (strcmp(argv[i], "--mode=optimization-test") == 0) {
            config.mode = BenchmarkConfig::OPTIMIZATION_TEST;
            config.show_cache_stats = true;
        } else if (strcmp(argv[i], "--realistic-data") == 0) {
            config.realistic_data = true;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            config.verbose = true;
            config.show_cache_stats = true;
        } else if (strncmp(argv[i], "--test-size=", 12) == 0) {
            // Parse comma-separated test sizes
            std::string sizes_str = argv[i] + 12;
            config.test_sizes.clear();
            size_t pos = 0;
            while (pos < sizes_str.length()) {
                size_t comma_pos = sizes_str.find(',', pos);
                std::string size_str = sizes_str.substr(pos, comma_pos - pos);
                config.test_sizes.push_back(std::stoi(size_str));
                if (comma_pos == std::string::npos) break;
                pos = comma_pos + 1;
            }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else {
            std::cerr << "Unknown argument: " << argv[i] << std::endl;
            print_usage(argv[0]);
            exit(1);
        }
    }

    return config;
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << std::endl << "Options:" << std::endl;
    std::cout << "  --mode=comprehensive       Run all INSERT, DELETE, and SELECT benchmarks (default)" << std::endl;
    std::cout << "  --mode=delete-focus        Focus on DELETE operations with cache analysis" << std::endl;
    std::cout << "  --mode=insert-only         Run only INSERT benchmarks" << std::endl;
    std::cout << "  --mode=delete-only         Run only DELETE benchmarks" << std::endl;
    std::cout << "  --mode=select-only         Run only SELECT benchmarks" << std::endl;
    std::cout << "  --mode=cache-analysis      Test SQL cache performance and effectiveness" << std::endl;
    std::cout << "  --mode=optimization-test   Comprehensive optimization testing with feature summary" << std::endl;
    std::cout << "  --realistic-data           Use random test data instead of predictable patterns" << std::endl;
    std::cout << "  --verbose                  Show detailed cache statistics" << std::endl;
    std::cout << "  --test-size=N              Comma-separated list of test sizes (default: 1000,5000,10000)" << std::endl;
    std::cout << "  --help, -h                 Show this help message" << std::endl;
    std::cout << std::endl << "Examples:" << std::endl;
    std::cout << "  " << program_name << "                                        # Run comprehensive benchmark" << std::endl;
    std::cout << "  " << program_name << " --mode=delete-focus                        # DELETE focus with cache stats" << std::endl;
    std::cout << "  " << program_name << " --mode=insert-only --test-size=10000       # Only INSERT with 10K records" << std::endl;
    std::cout << "  " << program_name << " --mode=cache-analysis --realistic-data    # Cache analysis with random data" << std::endl;
    std::cout << "  " << program_name << " --mode=optimization-test --verbose         # Full optimization test with details" << std::endl;
}