#include <chrono>
#include <iostream>
#include <vector>
#include <iomanip>
#include <string>
#include "sqlite_orm_wrapper.h"

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

void benchmark_sqlite_orm_remove(int num_records) {
    // Setup sqlite_orm storage using wrapper
    sqlite_orm_storage_t storage = sqlite_orm_init(":memory:");
    if (!storage) {
        std::cerr << "Failed to initialize sqlite_orm storage\n";
        return;
    }

    // Prepare data
    std::vector<int> ids;
    ids.reserve(num_records);

    // Insert test data
    sqlite_orm_begin_transaction(storage);
    for (int i = 1; i <= num_records; ++i) {
        ids.push_back(i);
        std::string name = "Person" + std::to_string(i);
        sqlite_orm_insert_person(storage, i, name.c_str(), 20 + (i % 50));
    }
    sqlite_orm_commit_transaction(storage);

    // Benchmark removal
    BenchmarkTimer timer;
    double total_time = 0;
    int successful_removes = 0;

    for (int id : ids) {
        timer.reset();
        sqlite_orm_remove_person(storage, id);
        double elapsed = timer.elapsed_ms();
        successful_removes++;
        total_time += elapsed;
    }

    // Report results
    std::cout << "sqlite_orm - Remove " << num_records << " records:\n";
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms\n";
    std::cout << "  Average per remove: " << std::fixed << std::setprecision(4)
              << (total_time / successful_removes) << " ms\n";
    std::cout << "  Successful removes: " << successful_removes << "/" << num_records << "\n";

    // Cleanup
    sqlite_orm_cleanup(storage);
}

int main() {
    std::cout << "=== sqlite_orm Benchmark ===\n\n";

    const std::vector<int> test_sizes = {1000, 5000, 10000};

    for (int size : test_sizes) {
        std::cout << "--- Testing with " << size << " records ---\n";
        benchmark_sqlite_orm_remove(size);
        std::cout << "\n\n";
    }

    return 0;
}