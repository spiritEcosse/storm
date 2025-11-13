#include <chrono>
#include <iostream>
#include <vector>
#include <iomanip>
#include <string>
#include "sqlite_orm_wrapper.h"

// Forward declarations
void benchmark_sqlite_orm_single_insert(int num_records);
void benchmark_sqlite_orm_batch_insert(int num_records);
void benchmark_sqlite_orm_single_delete(int num_records);
void benchmark_sqlite_orm_batch_delete(int num_records);
void benchmark_sqlite_orm_select(int num_records);
void benchmark_sqlite_orm_single_update(int num_records);
void benchmark_sqlite_orm_batch_update(int num_records);

class BenchmarkTimer {
  public:
    BenchmarkTimer() : start_(std::chrono::high_resolution_clock::now()) {}

    double elapsed_ms() const {
        auto end      = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
        return duration.count() / 1000.0;
    }

    void reset() {
        start_ = std::chrono::high_resolution_clock::now();
    }

  private:
    std::chrono::high_resolution_clock::time_point start_;
};

void benchmark_sqlite_orm_single_insert(int num_records) {
    std::cout << "=== sqlite_orm Single INSERT Benchmark ===\n";

    // Setup sqlite_orm storage using wrapper
    sqlite_orm_storage_t storage = sqlite_orm_init(":memory:");
    if (!storage) {
        std::cerr << "Failed to initialize sqlite_orm storage\n";
        return;
    }

    // Prepare data - same pattern as Storm ORM
    struct PersonData {
        int         id;
        std::string name;
        int         age;
    };

    std::vector<PersonData> persons;
    persons.reserve(num_records);
    for (int i = 1; i <= num_records; ++i) {
        persons.push_back({i, "Person" + std::to_string(i), 20 + (i % 50)});
    }

    // Benchmark single INSERT operations
    BenchmarkTimer timer;
    double         total_time         = 0;
    int            successful_inserts = 0;

    for (const auto& person : persons) {
        timer.reset();
        sqlite_orm_insert_person(storage, person.id, person.name.c_str(), person.age);
        double elapsed = timer.elapsed_ms();
        successful_inserts++;
        total_time += elapsed;
    }

    // Report results - matching Storm ORM format
    std::cout << "sqlite_orm - Single INSERT " << num_records << " records:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
    std::cout << "  Average per insert: " << std::fixed << std::setprecision(4) << (total_time / successful_inserts)
              << " ms\n";
    std::cout << "  Successful inserts: " << successful_inserts << "/" << num_records << "\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (successful_inserts / (total_time / 1000.0))
              << " inserts/sec\n";

    // Cleanup
    sqlite_orm_cleanup(storage);
}

void benchmark_sqlite_orm_batch_insert(int num_records) {
    std::cout << "=== sqlite_orm Batch INSERT Benchmark ===\n";

    // Test different batch sizes to match Storm ORM benchmark
    const std::vector<size_t> batch_sizes = {1, 10, 25, 50, 100, 500, 1000};

    for (size_t batch_size : batch_sizes) {
        if (batch_size > static_cast<size_t>(num_records))
            continue;

        std::cout << "\n--- Batch size: " << batch_size << " ---\n";

        // Setup sqlite_orm storage using wrapper
        sqlite_orm_storage_t storage = sqlite_orm_init(":memory:");
        if (!storage) {
            std::cerr << "Failed to initialize sqlite_orm storage\n";
            continue;
        }

        // Prepare data - same pattern as Storm ORM
        struct PersonData {
            int         id;
            std::string name;
            int         age;
        };

        std::vector<PersonData> persons;
        persons.reserve(num_records);
        for (int i = 1; i <= num_records; ++i) {
            persons.push_back({i, "Person" + std::to_string(i), 20 + (i % 50)});
        }

        // Benchmark batch INSERT operations with transaction management
        BenchmarkTimer timer;
        double         total_time         = 0;
        int            successful_inserts = 0;
        int            batch_count        = 0;

        for (size_t i = 0; i < persons.size(); i += batch_size) {
            size_t end_idx            = std::min(i + batch_size, persons.size());
            size_t current_batch_size = end_idx - i;

            timer.reset();

            // Use transaction for batch operations to match optimal performance
            sqlite_orm_begin_transaction(storage);
            for (size_t j = i; j < end_idx; ++j) {
                const auto& person = persons[j];
                sqlite_orm_insert_person(storage, person.id, person.name.c_str(), person.age);
                successful_inserts++;
            }
            sqlite_orm_commit_transaction(storage);

            double elapsed = timer.elapsed_ms();
            total_time += elapsed;
            batch_count++;
        }

        // Report results - matching Storm ORM format
        std::cout << "sqlite_orm - Batch INSERT " << num_records << " records (batch size " << batch_size << "):\n";
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
        std::cout << "  Average per insert: " << std::fixed << std::setprecision(4) << (total_time / successful_inserts)
                  << " ms\n";
        std::cout << "  Average per batch: " << std::fixed << std::setprecision(4) << (total_time / batch_count)
                  << " ms\n";
        std::cout << "  Successful inserts: " << successful_inserts << "/" << num_records << "\n";
        std::cout << "  Batch count: " << batch_count << "\n";
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
                  << (successful_inserts / (total_time / 1000.0)) << " inserts/sec\n";

        // Cleanup
        sqlite_orm_cleanup(storage);
    }
}

void benchmark_sqlite_orm_select(int num_records) {
    std::cout << "=== sqlite_orm SELECT Benchmark ===\n";

    // Setup sqlite_orm storage using wrapper
    sqlite_orm_storage_t storage = sqlite_orm_init(":memory:");
    if (!storage) {
        std::cerr << "Failed to initialize sqlite_orm storage\n";
        return;
    }

    // Prepare and insert test data
    struct PersonData {
        int         id;
        std::string name;
        int         age;
    };

    std::vector<PersonData> persons;
    persons.reserve(num_records);
    for (int i = 1; i <= num_records; ++i) {
        persons.push_back({i, "Person" + std::to_string(i), 20 + (i % 50)});
    }

    // Insert test data
    for (const auto& person : persons) {
        sqlite_orm_insert_person(storage, person.id, person.name.c_str(), person.age);
    }

    // Benchmark SELECT operation (fetching all rows)
    BenchmarkTimer timer;
    timer.reset();
    int    rows_fetched = sqlite_orm_select_all_persons(storage);
    double elapsed      = timer.elapsed_ms();

    // Report results - matching Storm ORM format
    std::cout << "sqlite_orm - SELECT " << num_records << " records:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << elapsed << " ms\n";
    std::cout << "  Rows fetched: " << rows_fetched << "\n";
    std::cout << "  Average per row: " << std::fixed << std::setprecision(4) << (elapsed / rows_fetched) << " ms\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (rows_fetched / (elapsed / 1000.0))
              << " rows/sec\n";

    // Cleanup
    sqlite_orm_cleanup(storage);
}

int main() {
    std::cout << "=== sqlite_orm INSERT/DELETE Benchmark ===\n\n";

    const std::vector<int> test_sizes = {1000, 5000, 10000};

    for (int size : test_sizes) {
        std::cout << "========================================\n";
        std::cout << "Testing with " << size << " records\n";
        std::cout << "========================================\n\n";

        // Test single INSERT operations
        benchmark_sqlite_orm_single_insert(size);
        std::cout << "\n\n";

        // Test batch INSERT operations with different batch sizes
        benchmark_sqlite_orm_batch_insert(size);
        std::cout << "\n\n";

        // Test single DELETE operations
        benchmark_sqlite_orm_single_delete(size);
        std::cout << "\n\n";

        // Test batch DELETE operations with different batch sizes
        benchmark_sqlite_orm_batch_delete(size);
        std::cout << "\n\n";

        // Test SELECT operations
        benchmark_sqlite_orm_select(size);
        std::cout << "\n\n";

        // Test single UPDATE operations
        benchmark_sqlite_orm_single_update(size);
        std::cout << "\n\n";

        // Test batch UPDATE operations with different batch sizes
        benchmark_sqlite_orm_batch_update(size);
        std::cout << "\n\n";
    }

    return 0;
}

void benchmark_sqlite_orm_single_delete(int num_records) {
    std::cout << "=== sqlite_orm Single DELETE Benchmark ===\n";

    // Setup sqlite_orm storage using wrapper
    sqlite_orm_storage_t storage = sqlite_orm_init(":memory:");
    if (!storage) {
        std::cerr << "Failed to initialize sqlite_orm storage\n";
        return;
    }

    // Prepare data - same pattern as Storm ORM
    struct PersonData {
        int         id;
        std::string name;
        int         age;
    };

    std::vector<PersonData> persons;
    persons.reserve(num_records);
    for (int i = 1; i <= num_records; ++i) {
        persons.push_back({i, "Person" + std::to_string(i), 20 + (i % 50)});
    }

    // Insert test data first for deletion
    sqlite_orm_begin_transaction(storage);
    for (const auto& person : persons) {
        sqlite_orm_insert_person(storage, person.id, person.name.c_str(), person.age);
    }
    sqlite_orm_commit_transaction(storage);

    // Benchmark single DELETE operations
    BenchmarkTimer timer;
    double         total_time         = 0;
    int            successful_deletes = 0;

    for (const auto& person : persons) {
        timer.reset();
        sqlite_orm_remove_person(storage, person.id);
        double elapsed = timer.elapsed_ms();
        successful_deletes++;
        total_time += elapsed;
    }

    // Report results - matching Storm ORM format
    std::cout << "sqlite_orm - Single DELETE " << num_records << " records:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
    std::cout << "  Average per delete: " << std::fixed << std::setprecision(4) << (total_time / successful_deletes)
              << " ms\n";
    std::cout << "  Successful deletes: " << successful_deletes << "/" << num_records << "\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (successful_deletes / (total_time / 1000.0))
              << " deletes/sec\n";

    // Cleanup
    sqlite_orm_cleanup(storage);
}

void benchmark_sqlite_orm_batch_delete(int num_records) {
    std::cout << "=== sqlite_orm Batch DELETE Benchmark ===\n";

    // Test different batch sizes to match Storm ORM benchmark
    const std::vector<size_t> batch_sizes = {1, 10, 25, 50, 100, 500, 1000};

    for (size_t batch_size : batch_sizes) {
        if (batch_size > static_cast<size_t>(num_records))
            continue;

        std::cout << "\n--- Batch size: " << batch_size << " ---\n";

        // Setup sqlite_orm storage using wrapper
        sqlite_orm_storage_t storage = sqlite_orm_init(":memory:");
        if (!storage) {
            std::cerr << "Failed to initialize sqlite_orm storage\n";
            continue;
        }

        // Prepare data - same pattern as Storm ORM
        struct PersonData {
            int         id;
            std::string name;
            int         age;
        };

        std::vector<PersonData> persons;
        persons.reserve(num_records);
        for (int i = 1; i <= num_records; ++i) {
            persons.push_back({i, "Person" + std::to_string(i), 20 + (i % 50)});
        }

        // Insert test data first for deletion
        sqlite_orm_begin_transaction(storage);
        for (const auto& person : persons) {
            sqlite_orm_insert_person(storage, person.id, person.name.c_str(), person.age);
        }
        sqlite_orm_commit_transaction(storage);

        // Benchmark batch DELETE operations with transaction management
        BenchmarkTimer timer;
        double         total_time         = 0;
        int            successful_deletes = 0;
        int            batch_count        = 0;

        for (size_t i = 0; i < persons.size(); i += batch_size) {
            size_t end_idx            = std::min(i + batch_size, persons.size());
            size_t current_batch_size = end_idx - i;

            timer.reset();

            // Use transaction for batch operations to match optimal performance
            sqlite_orm_begin_transaction(storage);
            for (size_t j = i; j < end_idx; ++j) {
                const auto& person = persons[j];
                sqlite_orm_remove_person(storage, person.id);
                successful_deletes++;
            }
            sqlite_orm_commit_transaction(storage);

            double elapsed = timer.elapsed_ms();
            total_time += elapsed;
            batch_count++;
        }

        // Report results - matching Storm ORM format
        std::cout << "sqlite_orm - Batch DELETE " << num_records << " records (batch size " << batch_size << "):\n";
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
        std::cout << "  Average per delete: " << std::fixed << std::setprecision(4) << (total_time / successful_deletes)
                  << " ms\n";
        std::cout << "  Average per batch: " << std::fixed << std::setprecision(4) << (total_time / batch_count)
                  << " ms\n";
        std::cout << "  Successful deletes: " << successful_deletes << "/" << num_records << "\n";
        std::cout << "  Batch count: " << batch_count << "\n";
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
                  << (successful_deletes / (total_time / 1000.0)) << " deletes/sec\n";

        // Cleanup
        sqlite_orm_cleanup(storage);
    }
}

void benchmark_sqlite_orm_single_update(int num_records) {
    std::cout << "=== sqlite_orm Single UPDATE Benchmark ===\n";

    // Setup sqlite_orm storage using wrapper
    sqlite_orm_storage_t storage = sqlite_orm_init(":memory:");
    if (!storage) {
        std::cerr << "Failed to initialize sqlite_orm storage\n";
        return;
    }

    // Prepare data - same pattern as Storm ORM
    struct PersonData {
        int         id;
        std::string name;
        int         age;
    };

    std::vector<PersonData> persons;
    persons.reserve(num_records);
    for (int i = 1; i <= num_records; ++i) {
        persons.push_back({i, "Person" + std::to_string(i), 20 + (i % 50)});
    }

    // Insert test data first for update
    sqlite_orm_begin_transaction(storage);
    for (const auto& person : persons) {
        sqlite_orm_insert_person(storage, person.id, person.name.c_str(), person.age);
    }
    sqlite_orm_commit_transaction(storage);

    // Benchmark single UPDATE operations
    BenchmarkTimer timer;
    double         total_time         = 0;
    int            successful_updates = 0;

    for (auto& person : persons) {
        timer.reset();

        // Modify the data
        person.name += "_updated";
        person.age += 1;

        sqlite_orm_update_person(storage, person.id, person.name.c_str(), person.age);

        double elapsed = timer.elapsed_ms();
        successful_updates++;
        total_time += elapsed;
    }

    // Report results - matching Storm ORM format
    std::cout << "sqlite_orm - Single UPDATE " << num_records << " records:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
    std::cout << "  Average per update: " << std::fixed << std::setprecision(4) << (total_time / successful_updates)
              << " ms\n";
    std::cout << "  Successful updates: " << successful_updates << "/" << num_records << "\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (successful_updates / (total_time / 1000.0))
              << " updates/sec\n";

    // Cleanup
    sqlite_orm_cleanup(storage);
}

void benchmark_sqlite_orm_batch_update(int num_records) {
    std::cout << "=== sqlite_orm Batch UPDATE Benchmark ===\n";

    // Test different batch sizes to match Storm ORM benchmark
    const std::vector<size_t> batch_sizes = {1, 10, 25, 50, 100, 500, 1000};

    for (size_t batch_size : batch_sizes) {
        if (batch_size > static_cast<size_t>(num_records))
            continue;

        std::cout << "\n--- Batch size: " << batch_size << " ---\n";

        // Setup sqlite_orm storage using wrapper
        sqlite_orm_storage_t storage = sqlite_orm_init(":memory:");
        if (!storage) {
            std::cerr << "Failed to initialize sqlite_orm storage\n";
            continue;
        }

        // Prepare data - same pattern as Storm ORM
        struct PersonData {
            int         id;
            std::string name;
            int         age;
        };

        std::vector<PersonData> persons;
        persons.reserve(num_records);
        for (int i = 1; i <= num_records; ++i) {
            persons.push_back({i, "Person" + std::to_string(i), 20 + (i % 50)});
        }

        // Insert test data first for update
        sqlite_orm_begin_transaction(storage);
        for (const auto& person : persons) {
            sqlite_orm_insert_person(storage, person.id, person.name.c_str(), person.age);
        }
        sqlite_orm_commit_transaction(storage);

        // Benchmark batch UPDATE operations with transaction management
        BenchmarkTimer timer;
        double         total_time         = 0;
        int            successful_updates = 0;
        int            batch_count        = 0;

        for (size_t i = 0; i < persons.size(); i += batch_size) {
            size_t end_idx            = std::min(i + batch_size, persons.size());
            size_t current_batch_size = end_idx - i;

            timer.reset();

            // Use transaction for batch operations to match optimal performance
            sqlite_orm_begin_transaction(storage);
            for (size_t j = i; j < end_idx; ++j) {
                auto& person = persons[j];
                // Modify the data
                person.name += "_updated";
                person.age += 1;
                sqlite_orm_update_person(storage, person.id, person.name.c_str(), person.age);
                successful_updates++;
            }
            sqlite_orm_commit_transaction(storage);

            double elapsed = timer.elapsed_ms();
            total_time += elapsed;
            batch_count++;
        }

        // Report results - matching Storm ORM format
        std::cout << "sqlite_orm - Batch UPDATE " << num_records << " records (batch size " << batch_size << "):\n";
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
        std::cout << "  Average per update: " << std::fixed << std::setprecision(4) << (total_time / successful_updates)
                  << " ms\n";
        std::cout << "  Average per batch: " << std::fixed << std::setprecision(4) << (total_time / batch_count)
                  << " ms\n";
        std::cout << "  Successful updates: " << successful_updates << "/" << num_records << "\n";
        std::cout << "  Batch count: " << batch_count << "\n";
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
                  << (successful_updates / (total_time / 1000.0)) << " updates/sec\n";

        // Cleanup
        sqlite_orm_cleanup(storage);
    }
}