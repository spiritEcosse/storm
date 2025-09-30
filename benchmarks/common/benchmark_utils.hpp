#pragma once

#include <chrono>
#include <iostream>
#include <vector>
#include <iomanip>
#include <string>

namespace storm::benchmark {

// Base timer class with common functionality
class BaseTimer {
public:
    BaseTimer() : start_(std::chrono::high_resolution_clock::now()) {}

    void reset() {
        start_ = std::chrono::high_resolution_clock::now();
    }

protected:
    std::chrono::high_resolution_clock::time_point start_;
};

// Macro-benchmark timer with millisecond precision
class BenchmarkTimer : public BaseTimer {
public:
    double elapsed_ms() const {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
        return duration.count() / 1000.0;
    }
};

// Micro-benchmark timer with nanosecond precision
class MicroBenchmarkTimer : public BaseTimer {
public:
    double elapsed_ns() const {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_);
        return duration.count();
    }

    double elapsed_us() const {
        return elapsed_ns() / 1000.0;
    }
};

// Common database setup utilities
namespace db_utils {

// Standard Person table creation SQL
constexpr const char* PERSON_TABLE_SQL =
    "CREATE TABLE Person ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
    "name TEXT NOT NULL, "
    "age INTEGER NOT NULL"
    ")";

} // namespace db_utils

// Common data generation utilities
namespace data_utils {

// Generate simple test data with predictable patterns
// Template version that works with any Person-like struct
template<typename PersonType>
inline std::vector<PersonType> generate_simple_test_data(int count, int start_id = 1) {
    std::vector<PersonType> persons;
    persons.reserve(count);
    for (int i = 0; i < count; ++i) {
        persons.push_back({
            start_id + i,
            "Person" + std::to_string(start_id + i),
            20 + ((start_id + i) % 50)
        });
    }
    return persons;
}

// Generate test data for specific ID range (useful for micro-benchmarks)
// Template version that works with any Person-like struct
template<typename PersonType>
inline std::vector<PersonType> generate_test_data_range(size_t count, int start_id) {
    std::vector<PersonType> persons;
    persons.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        persons.push_back({
            start_id + static_cast<int>(i),
            "Test" + std::to_string(i),
            25
        });
    }
    return persons;
}

} // namespace data_utils

// Common output formatting utilities
namespace format_utils {

// Print a standard benchmark header
inline void print_benchmark_header(const std::string& benchmark_name, const std::string& description = "") {
    std::cout << "=== " << benchmark_name << " ===" << std::endl;
    if (!description.empty()) {
        std::cout << description << std::endl;
    }
    std::cout << std::endl;
}

// Print benchmark results with consistent formatting
inline void print_benchmark_result(const std::string& test_name, int record_count,
                                  double total_time, int successful_ops, int total_ops,
                                  const std::string& unit = "operations") {
    std::cout << test_name << " " << record_count << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Average per " << unit.substr(0, unit.find('s')) << ": "
              << std::fixed << std::setprecision(4) << (total_time / successful_ops) << " ms" << std::endl;
    std::cout << "  Successful " << unit << ": " << successful_ops << "/" << total_ops << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (successful_ops / (total_time / 1000.0)) << " " << unit << "/sec" << std::endl;
}

} // namespace format_utils

} // namespace storm::benchmark