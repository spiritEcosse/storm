#include <chrono>
#include <iostream>
#include <vector>
#include <iomanip>
#include <string>
#include <sqlite3.h>

// Forward declarations
void benchmark_pure_sqlite_single_insert(int num_records);
void benchmark_raw_sqlite_single_insert(int num_records);
void benchmark_raw_sqlite_batch_insert(int num_records);
void benchmark_pure_sqlite_single_delete(int num_records);
void benchmark_raw_sqlite_single_delete(int num_records);
void benchmark_raw_sqlite_batch_delete(int num_records);
void benchmark_raw_sqlite_single_update(int num_records);
void benchmark_raw_sqlite_batch_update(int num_records);
void benchmark_raw_sqlite_select(int num_records);

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

// Benchmark using pure sqlite3 single INSERT operations (most basic approach)
void benchmark_pure_sqlite_single_insert(int num_records) {
    std::cout << "=== Pure SQLite3 Single INSERT Benchmark ===\n";

    sqlite3* db;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    // Create table
    const char* create_sql = "CREATE TABLE Person (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, age INTEGER NOT NULL)";
    rc = sqlite3_exec(db, create_sql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return;
    }

    // Benchmark INSERT operations using simple exec calls
    BenchmarkTimer timer;
    double total_time = 0;
    int successful_inserts = 0;

    for (int i = 1; i <= num_records; ++i) {
        timer.reset();

        std::string insert_sql = "INSERT INTO Person (id, name, age) VALUES (" +
                                std::to_string(i) + ", 'Person" + std::to_string(i) + "', " +
                                std::to_string(20 + (i % 50)) + ")";
        rc = sqlite3_exec(db, insert_sql.c_str(), nullptr, nullptr, nullptr);

        double elapsed = timer.elapsed_ms();

        if (rc == SQLITE_OK) {
            successful_inserts++;
            total_time += elapsed;
        }
    }

    // Report results
    std::cout << "Pure SQLite3 - Single INSERT " << num_records << " records:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
    std::cout << "  Average per insert: " << std::fixed << std::setprecision(4)
              << (total_time / successful_inserts) << " ms\n";
    std::cout << "  Successful inserts: " << successful_inserts << "/" << num_records << "\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (successful_inserts / (total_time / 1000.0)) << " inserts/sec\n";

    sqlite3_close(db);
}

// Benchmark using raw SQLite with prepared statements (representing Storm's approach)
void benchmark_raw_sqlite_single_insert(int num_records) {
    std::cout << "=== Raw SQLite Single INSERT Benchmark (prepared statements) ===\n";

    sqlite3* db;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    // Create table
    const char* create_sql = "CREATE TABLE Person (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, age INTEGER NOT NULL)";
    rc = sqlite3_exec(db, create_sql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return;
    }

    // Prepare INSERT statement
    sqlite3_stmt* insert_stmt;
    sqlite3_prepare_v2(db, "INSERT INTO Person (id, name, age) VALUES (?, ?, ?)", -1, &insert_stmt, nullptr);

    // Benchmark INSERT operations
    BenchmarkTimer timer;
    double total_time = 0;
    int successful_inserts = 0;

    for (int i = 1; i <= num_records; ++i) {
        timer.reset();

        sqlite3_bind_int(insert_stmt, 1, i);
        std::string name = "Person" + std::to_string(i);
        sqlite3_bind_text(insert_stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(insert_stmt, 3, 20 + (i % 50));
        rc = sqlite3_step(insert_stmt);

        // Retrieve auto-generated ID (matching Storm ORM behavior)
        sqlite3_int64 last_id = sqlite3_last_insert_rowid(db);
        (void)last_id; // Prevent compiler optimization

        double elapsed = timer.elapsed_ms();

        if (rc == SQLITE_DONE) {
            successful_inserts++;
            total_time += elapsed;
        }

        sqlite3_reset(insert_stmt);
    }

    sqlite3_finalize(insert_stmt);

    // Report results
    std::cout << "Raw SQLite (prepared statements) - Single INSERT " << num_records << " records:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
    std::cout << "  Average per insert: " << std::fixed << std::setprecision(4)
              << (total_time / successful_inserts) << " ms\n";
    std::cout << "  Successful inserts: " << successful_inserts << "/" << num_records << "\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (successful_inserts / (total_time / 1000.0)) << " inserts/sec\n";

    sqlite3_close(db);
}

// Benchmark using raw SQLite with batch INSERT operations
void benchmark_raw_sqlite_batch_insert(int num_records) {
    std::cout << "=== Raw SQLite Batch INSERT Benchmark ===\n";

    // Test different batch sizes to find optimal performance
    const std::vector<size_t> batch_sizes = {1, 10, 25, 50, 100, 500, 1000};

    for (size_t batch_size : batch_sizes) {
        if (batch_size > static_cast<size_t>(num_records)) continue;

        std::cout << "\n--- Batch size: " << batch_size << " ---\n";

        sqlite3* db;
        int rc = sqlite3_open(":memory:", &db);
        if (rc != SQLITE_OK) {
            std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
            continue;
        }

        // Create table
        const char* create_sql = "CREATE TABLE Person (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, age INTEGER NOT NULL)";
        rc = sqlite3_exec(db, create_sql, nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
            sqlite3_close(db);
            continue;
        }

        // Benchmark batch INSERT operations using transactions
        BenchmarkTimer timer;
        double total_time = 0;
        int successful_inserts = 0;
        int batch_count = 0;

        sqlite3_stmt* insert_stmt;
        sqlite3_prepare_v2(db, "INSERT INTO Person (id, name, age) VALUES (?, ?, ?)", -1, &insert_stmt, nullptr);

        for (size_t i = 0; i < static_cast<size_t>(num_records); i += batch_size) {
            size_t end_idx = std::min(i + batch_size, static_cast<size_t>(num_records));
            size_t current_batch_size = end_idx - i;

            timer.reset();

            // Begin transaction for batch
            sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

            for (size_t j = i; j < end_idx; ++j) {
                int id = static_cast<int>(j + 1);
                sqlite3_bind_int(insert_stmt, 1, id);
                std::string name = "Person" + std::to_string(id);
                sqlite3_bind_text(insert_stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(insert_stmt, 3, 20 + (id % 50));
                sqlite3_step(insert_stmt);
                sqlite3_reset(insert_stmt);
            }

            // Commit transaction
            sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

            double elapsed = timer.elapsed_ms();
            successful_inserts += current_batch_size;
            total_time += elapsed;
            batch_count++;
        }

        sqlite3_finalize(insert_stmt);

        // Report results
        std::cout << "Raw SQLite - Batch INSERT " << num_records << " records (batch size " << batch_size << "):\n";
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
        std::cout << "  Average per insert: " << std::fixed << std::setprecision(4)
                  << (total_time / successful_inserts) << " ms\n";
        std::cout << "  Average per batch: " << std::fixed << std::setprecision(4)
                  << (total_time / batch_count) << " ms\n";
        std::cout << "  Successful inserts: " << successful_inserts << "/" << num_records << "\n";
        std::cout << "  Batch count: " << batch_count << "\n";
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
                  << (successful_inserts / (total_time / 1000.0)) << " inserts/sec\n";

        sqlite3_close(db);
    }
}

int main() {
    std::cout << "=== SQLite INSERT/DELETE Benchmark ===\n\n";

    const std::vector<int> test_sizes = {1000, 5000, 10000};

    for (int size : test_sizes) {
        std::cout << "========================================\n";
        std::cout << "Testing with " << size << " records\n";
        std::cout << "========================================\n\n";

        // Test basic SQLite exec-based INSERT operations
        benchmark_pure_sqlite_single_insert(size);
        std::cout << "\n\n";

        // Test prepared statement INSERT operations
        benchmark_raw_sqlite_single_insert(size);
        std::cout << "\n\n";

        // Test batch INSERT operations with different batch sizes
        benchmark_raw_sqlite_batch_insert(size);
        std::cout << "\n\n";

        // Test basic SQLite exec-based DELETE operations
        benchmark_pure_sqlite_single_delete(size);
        std::cout << "\n\n";

        // Test prepared statement DELETE operations
        benchmark_raw_sqlite_single_delete(size);
        std::cout << "\n\n";

        // Test batch DELETE operations with different batch sizes
        benchmark_raw_sqlite_batch_delete(size);
        std::cout << "\n\n";

        // Test prepared statement UPDATE operations
        benchmark_raw_sqlite_single_update(size);
        std::cout << "\n\n";

        // Test batch UPDATE operations with different batch sizes
        benchmark_raw_sqlite_batch_update(size);
        std::cout << "\n\n";

        // Test SELECT operations
        benchmark_raw_sqlite_select(size);
        std::cout << "\n\n";
    }

    return 0;
}

// Benchmark using pure sqlite3 single DELETE operations (most basic approach)
void benchmark_pure_sqlite_single_delete(int num_records) {
    std::cout << "=== Pure SQLite3 Single DELETE Benchmark ===\n";

    sqlite3* db;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    // Create table
    const char* create_sql = "CREATE TABLE Person (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, age INTEGER NOT NULL)";
    rc = sqlite3_exec(db, create_sql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return;
    }

    // Insert test data first with transaction for setup
    rc = sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
    for (int i = 1; i <= num_records; ++i) {
        std::string insert_sql = "INSERT INTO Person (id, name, age) VALUES (" +
                                std::to_string(i) + ", 'Person" + std::to_string(i) + "', " +
                                std::to_string(20 + (i % 50)) + ")";
        sqlite3_exec(db, insert_sql.c_str(), nullptr, nullptr, nullptr);
    }
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

    // Benchmark DELETE operations using simple exec calls
    BenchmarkTimer timer;
    double total_time = 0;
    int successful_deletes = 0;

    for (int i = 1; i <= num_records; ++i) {
        timer.reset();

        std::string delete_sql = "DELETE FROM Person WHERE id = " + std::to_string(i);
        rc = sqlite3_exec(db, delete_sql.c_str(), nullptr, nullptr, nullptr);

        double elapsed = timer.elapsed_ms();

        if (rc == SQLITE_OK) {
            successful_deletes++;
            total_time += elapsed;
        }
    }

    // Report results
    std::cout << "Pure SQLite3 - Single DELETE " << num_records << " records:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
    std::cout << "  Average per delete: " << std::fixed << std::setprecision(4)
              << (total_time / successful_deletes) << " ms\n";
    std::cout << "  Successful deletes: " << successful_deletes << "/" << num_records << "\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (successful_deletes / (total_time / 1000.0)) << " deletes/sec\n";

    sqlite3_close(db);
}

// Benchmark using raw SQLite with prepared statements (representing Storm's approach)
void benchmark_raw_sqlite_single_delete(int num_records) {
    std::cout << "=== Raw SQLite Single DELETE Benchmark (prepared statements) ===\n";

    sqlite3* db;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    // Create table
    const char* create_sql = "CREATE TABLE Person (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, age INTEGER NOT NULL)";
    rc = sqlite3_exec(db, create_sql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return;
    }

    // Insert test data first with transaction for setup
    rc = sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
    sqlite3_stmt* insert_stmt;
    sqlite3_prepare_v2(db, "INSERT INTO Person (id, name, age) VALUES (?, ?, ?)", -1, &insert_stmt, nullptr);
    for (int i = 1; i <= num_records; ++i) {
        sqlite3_bind_int(insert_stmt, 1, i);
        std::string name = "Person" + std::to_string(i);
        sqlite3_bind_text(insert_stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(insert_stmt, 3, 20 + (i % 50));
        sqlite3_step(insert_stmt);
        sqlite3_reset(insert_stmt);
    }
    sqlite3_finalize(insert_stmt);
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

    // Prepare DELETE statement
    sqlite3_stmt* delete_stmt;
    sqlite3_prepare_v2(db, "DELETE FROM Person WHERE id = ?", -1, &delete_stmt, nullptr);

    // Benchmark DELETE operations
    BenchmarkTimer timer;
    double total_time = 0;
    int successful_deletes = 0;

    for (int i = 1; i <= num_records; ++i) {
        timer.reset();

        sqlite3_bind_int(delete_stmt, 1, i);
        rc = sqlite3_step(delete_stmt);

        double elapsed = timer.elapsed_ms();

        if (rc == SQLITE_DONE) {
            successful_deletes++;
            total_time += elapsed;
        }

        sqlite3_reset(delete_stmt);
    }

    sqlite3_finalize(delete_stmt);

    // Report results
    std::cout << "Raw SQLite (prepared statements) - Single DELETE " << num_records << " records:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
    std::cout << "  Average per delete: " << std::fixed << std::setprecision(4)
              << (total_time / successful_deletes) << " ms\n";
    std::cout << "  Successful deletes: " << successful_deletes << "/" << num_records << "\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (successful_deletes / (total_time / 1000.0)) << " deletes/sec\n";

    sqlite3_close(db);
}

// Benchmark using raw SQLite with batch DELETE operations
void benchmark_raw_sqlite_batch_delete(int num_records) {
    std::cout << "=== Raw SQLite Batch DELETE Benchmark ===\n";

    // Test different batch sizes to find optimal performance
    const std::vector<size_t> batch_sizes = {1, 10, 25, 50, 100, 500, 1000};

    for (size_t batch_size : batch_sizes) {
        if (batch_size > static_cast<size_t>(num_records)) continue;

        std::cout << "\n--- Batch size: " << batch_size << " ---\n";

        sqlite3* db;
        int rc = sqlite3_open(":memory:", &db);
        if (rc != SQLITE_OK) {
            std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
            continue;
        }

        // Create table
        const char* create_sql = "CREATE TABLE Person (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, age INTEGER NOT NULL)";
        rc = sqlite3_exec(db, create_sql, nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
            sqlite3_close(db);
            continue;
        }

        // Insert test data first with transaction for setup
        rc = sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
        sqlite3_stmt* insert_stmt;
        sqlite3_prepare_v2(db, "INSERT INTO Person (id, name, age) VALUES (?, ?, ?)", -1, &insert_stmt, nullptr);
        for (int i = 1; i <= num_records; ++i) {
            sqlite3_bind_int(insert_stmt, 1, i);
            std::string name = "Person" + std::to_string(i);
            sqlite3_bind_text(insert_stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(insert_stmt, 3, 20 + (i % 50));
            sqlite3_step(insert_stmt);
            sqlite3_reset(insert_stmt);
        }
        sqlite3_finalize(insert_stmt);
        sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

        // Benchmark batch DELETE operations using transactions
        BenchmarkTimer timer;
        double total_time = 0;
        int successful_deletes = 0;
        int batch_count = 0;

        sqlite3_stmt* delete_stmt;
        sqlite3_prepare_v2(db, "DELETE FROM Person WHERE id = ?", -1, &delete_stmt, nullptr);

        for (size_t i = 0; i < static_cast<size_t>(num_records); i += batch_size) {
            size_t end_idx = std::min(i + batch_size, static_cast<size_t>(num_records));
            size_t current_batch_size = end_idx - i;

            timer.reset();

            // Begin transaction for batch
            sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

            for (size_t j = i; j < end_idx; ++j) {
                int id = static_cast<int>(j + 1);
                sqlite3_bind_int(delete_stmt, 1, id);
                sqlite3_step(delete_stmt);
                sqlite3_reset(delete_stmt);
            }

            // Commit transaction
            sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

            double elapsed = timer.elapsed_ms();
            successful_deletes += current_batch_size;
            total_time += elapsed;
            batch_count++;
        }

        sqlite3_finalize(delete_stmt);

        // Report results
        std::cout << "Raw SQLite - Batch DELETE " << num_records << " records (batch size " << batch_size << "):\n";
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
        std::cout << "  Average per delete: " << std::fixed << std::setprecision(4)
                  << (total_time / successful_deletes) << " ms\n";
        std::cout << "  Average per batch: " << std::fixed << std::setprecision(4)
                  << (total_time / batch_count) << " ms\n";
        std::cout << "  Successful deletes: " << successful_deletes << "/" << num_records << "\n";
        std::cout << "  Batch count: " << batch_count << "\n";
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
                  << (successful_deletes / (total_time / 1000.0)) << " deletes/sec\n";

        sqlite3_close(db);
    }
}

// Benchmark using raw SQLite with prepared statements for UPDATE operations
void benchmark_raw_sqlite_single_update(int num_records) {
    std::cout << "=== Raw SQLite Single UPDATE Benchmark (prepared statements) ===\n";

    sqlite3* db;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    // Create table
    const char* create_sql = "CREATE TABLE Person (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, age INTEGER NOT NULL)";
    rc = sqlite3_exec(db, create_sql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return;
    }

    // Insert test data first with transaction for setup
    rc = sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
    sqlite3_stmt* insert_stmt;
    sqlite3_prepare_v2(db, "INSERT INTO Person (id, name, age) VALUES (?, ?, ?)", -1, &insert_stmt, nullptr);
    for (int i = 1; i <= num_records; ++i) {
        sqlite3_bind_int(insert_stmt, 1, i);
        std::string name = "Person" + std::to_string(i);
        sqlite3_bind_text(insert_stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(insert_stmt, 3, 20 + (i % 50));
        sqlite3_step(insert_stmt);
        sqlite3_reset(insert_stmt);
    }
    sqlite3_finalize(insert_stmt);
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

    // Prepare UPDATE statement
    sqlite3_stmt* update_stmt;
    sqlite3_prepare_v2(db, "UPDATE Person SET name=?, age=? WHERE id=?", -1, &update_stmt, nullptr);

    // Benchmark UPDATE operations
    BenchmarkTimer timer;
    double total_time = 0;
    int successful_updates = 0;

    for (int i = 1; i <= num_records; ++i) {
        timer.reset();

        std::string new_name = "Person" + std::to_string(i) + "_updated";
        int new_age = 21 + (i % 50);
        sqlite3_bind_text(update_stmt, 1, new_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(update_stmt, 2, new_age);
        sqlite3_bind_int(update_stmt, 3, i);
        rc = sqlite3_step(update_stmt);

        double elapsed = timer.elapsed_ms();

        if (rc == SQLITE_DONE) {
            successful_updates++;
            total_time += elapsed;
        }

        sqlite3_reset(update_stmt);
    }

    sqlite3_finalize(update_stmt);

    // Report results
    std::cout << "Raw SQLite (prepared statements) - Single UPDATE " << num_records << " records:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
    std::cout << "  Average per update: " << std::fixed << std::setprecision(4)
              << (total_time / successful_updates) << " ms\n";
    std::cout << "  Successful updates: " << successful_updates << "/" << num_records << "\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (successful_updates / (total_time / 1000.0)) << " updates/sec\n";

    sqlite3_close(db);
}

// Benchmark using raw SQLite with batch UPDATE operations
void benchmark_raw_sqlite_batch_update(int num_records) {
    std::cout << "=== Raw SQLite Batch UPDATE Benchmark ===\n";

    // Test different batch sizes to find optimal performance
    const std::vector<size_t> batch_sizes = {1, 10, 25, 50, 100, 500, 1000};

    for (size_t batch_size : batch_sizes) {
        if (batch_size > static_cast<size_t>(num_records)) continue;

        std::cout << "\n--- Batch size: " << batch_size << " ---\n";

        sqlite3* db;
        int rc = sqlite3_open(":memory:", &db);
        if (rc != SQLITE_OK) {
            std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
            continue;
        }

        // Create table
        const char* create_sql = "CREATE TABLE Person (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, age INTEGER NOT NULL)";
        rc = sqlite3_exec(db, create_sql, nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK) {
            std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
            sqlite3_close(db);
            continue;
        }

        // Insert test data first with transaction for setup
        rc = sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
        sqlite3_stmt* insert_stmt;
        sqlite3_prepare_v2(db, "INSERT INTO Person (id, name, age) VALUES (?, ?, ?)", -1, &insert_stmt, nullptr);
        for (int i = 1; i <= num_records; ++i) {
            sqlite3_bind_int(insert_stmt, 1, i);
            std::string name = "Person" + std::to_string(i);
            sqlite3_bind_text(insert_stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(insert_stmt, 3, 20 + (i % 50));
            sqlite3_step(insert_stmt);
            sqlite3_reset(insert_stmt);
        }
        sqlite3_finalize(insert_stmt);
        sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

        // Benchmark batch UPDATE operations using transactions
        BenchmarkTimer timer;
        double total_time = 0;
        int successful_updates = 0;
        int batch_count = 0;

        sqlite3_stmt* update_stmt;
        sqlite3_prepare_v2(db, "UPDATE Person SET name=?, age=? WHERE id=?", -1, &update_stmt, nullptr);

        for (size_t i = 0; i < static_cast<size_t>(num_records); i += batch_size) {
            size_t end_idx = std::min(i + batch_size, static_cast<size_t>(num_records));
            size_t current_batch_size = end_idx - i;

            timer.reset();

            // Begin transaction for batch
            sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);

            for (size_t j = i; j < end_idx; ++j) {
                int id = static_cast<int>(j + 1);
                std::string new_name = "Person" + std::to_string(id) + "_updated";
                int new_age = 21 + (id % 50);
                sqlite3_bind_text(update_stmt, 1, new_name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(update_stmt, 2, new_age);
                sqlite3_bind_int(update_stmt, 3, id);
                sqlite3_step(update_stmt);
                sqlite3_reset(update_stmt);
            }

            // Commit transaction
            sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

            double elapsed = timer.elapsed_ms();
            successful_updates += current_batch_size;
            total_time += elapsed;
            batch_count++;
        }

        sqlite3_finalize(update_stmt);

        // Report results
        std::cout << "Raw SQLite - Batch UPDATE " << num_records << " records (batch size " << batch_size << "):\n";
        std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
        std::cout << "  Average per update: " << std::fixed << std::setprecision(4)
                  << (total_time / successful_updates) << " ms\n";
        std::cout << "  Average per batch: " << std::fixed << std::setprecision(4)
                  << (total_time / batch_count) << " ms\n";
        std::cout << "  Successful updates: " << successful_updates << "/" << num_records << "\n";
        std::cout << "  Batch count: " << batch_count << "\n";
        std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
                  << (successful_updates / (total_time / 1000.0)) << " updates/sec\n";

        sqlite3_close(db);
    }
}

// Benchmark using raw SQLite SELECT operations
void benchmark_raw_sqlite_select(int num_records) {
    std::cout << "=== Raw SQLite SELECT Benchmark ===\n";

    sqlite3* db;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    // Create table
    const char* create_sql = "CREATE TABLE Person (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, age INTEGER NOT NULL)";
    rc = sqlite3_exec(db, create_sql, nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot create table: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return;
    }

    // Insert test data first with transaction for setup
    rc = sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
    sqlite3_stmt* insert_stmt;
    sqlite3_prepare_v2(db, "INSERT INTO Person (id, name, age) VALUES (?, ?, ?)", -1, &insert_stmt, nullptr);
    for (int i = 1; i <= num_records; ++i) {
        sqlite3_bind_int(insert_stmt, 1, i);
        std::string name = "Person" + std::to_string(i);
        sqlite3_bind_text(insert_stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(insert_stmt, 3, 20 + (i % 50));
        sqlite3_step(insert_stmt);
        sqlite3_reset(insert_stmt);
    }
    sqlite3_finalize(insert_stmt);
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);

    // Prepare SELECT statement
    sqlite3_stmt* select_stmt;
    sqlite3_prepare_v2(db, "SELECT id, name, age FROM Person", -1, &select_stmt, nullptr);

    // Benchmark SELECT operation (fetching all rows)
    BenchmarkTimer timer;
    timer.reset();

    // Create objects just like Storm ORM does
    struct Person {
        int id;
        std::string name;
        int age;
    };
    std::vector<Person> persons;
    persons.reserve(num_records); // Pre-allocate to match Storm ORM's optimization

    while (sqlite3_step(select_stmt) == SQLITE_ROW) {
        // Extract values and create Person object
        int id = sqlite3_column_int(select_stmt, 0);
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(select_stmt, 1));
        int age = sqlite3_column_int(select_stmt, 2);

        // Create Person object and add to vector (matching Storm ORM behavior)
        persons.push_back(Person{id, std::string(name), age});
    }

    double elapsed = timer.elapsed_ms();
    int rows_fetched = persons.size();

    sqlite3_finalize(select_stmt);

    // Report results
    std::cout << "Raw SQLite - SELECT " << num_records << " records:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << elapsed << " ms\n";
    std::cout << "  Rows fetched: " << rows_fetched << "\n";
    std::cout << "  Average per row: " << std::fixed << std::setprecision(4)
              << (elapsed / rows_fetched) << " ms\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (rows_fetched / (elapsed / 1000.0)) << " rows/sec\n";

    sqlite3_close(db);
}