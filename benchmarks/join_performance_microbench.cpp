#include <chrono>
#include <iostream>
#include <iomanip>
#include <vector>

import storm;
import <string>;
import <expected>;

using namespace storm;

// Test models matching FK tests
struct User {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
};

struct Message {
    [[= storm::meta::FieldAttr::primary]] int id;
    [[= storm::meta::FieldAttr::fk]] User     sender;
    [[= storm::meta::FieldAttr::fk]] User     receiver;
    std::string                               text;
};

// High-resolution timer
class Timer {
    using clock_t      = std::chrono::high_resolution_clock;
    using time_point_t = std::chrono::time_point<clock_t>;

    time_point_t start_;

  public:
    Timer() : start_(clock_t::now()) {}

    void reset() {
        start_ = clock_t::now();
    }

    double elapsed_us() const {
        auto end = clock_t::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start_).count();
    }

    double elapsed_ms() const {
        return elapsed_us() / 1000.0;
    }
};

// Setup test database with sample data
void setup_database(int num_users, int num_messages) {
    auto result = QuerySet<User>::set_default_connection(":memory:");
    if (!result.has_value()) {
        std::cerr << "Failed to open database: " << result.error().message() << std::endl;
        exit(1);
    }

    auto& conn = QuerySet<User>::get_default_connection();

    // Create User table
    auto create_user = conn.execute(
            "CREATE TABLE User ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL, "
            "age INTEGER NOT NULL"
            ")"
    );
    if (!create_user.has_value()) {
        std::cerr << "Failed to create User table: " << create_user.error().message() << std::endl;
        exit(1);
    }

    // Create Message table
    auto create_message = conn.execute(
            "CREATE TABLE Message ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "sender_id INTEGER NOT NULL, "
            "receiver_id INTEGER NOT NULL, "
            "text TEXT NOT NULL"
            ")"
    );
    if (!create_message.has_value()) {
        std::cerr << "Failed to create Message table: " << create_message.error().message() << std::endl;
        exit(1);
    }

    // Insert users
    QuerySet<User>       user_qs;
    std::vector<int64_t> user_ids;
    user_ids.reserve(num_users);

    for (int i = 0; i < num_users; ++i) {
        User user{0, "User" + std::to_string(i), 20 + (i % 50)};
        auto insert_result = user_qs.insert(user);
        if (!insert_result.has_value()) {
            std::cerr << "Failed to insert user: " << insert_result.error().message() << std::endl;
            exit(1);
        }
        user_ids.push_back(insert_result.value());
    }

    // Insert messages (round-robin sender/receiver)
    QuerySet<Message> message_qs;
    for (int i = 0; i < num_messages; ++i) {
        int sender_idx   = i % num_users;
        int receiver_idx = (i + 1) % num_users;

        Message msg{
                0,
                User{static_cast<int>(user_ids[sender_idx]), "", 0},
                User{static_cast<int>(user_ids[receiver_idx]), "", 0},
                "Message " + std::to_string(i)
        };

        auto insert_result = message_qs.insert(msg);
        if (!insert_result.has_value()) {
            std::cerr << "Failed to insert message: " << insert_result.error().message() << std::endl;
            exit(1);
        }
    }
}

// Benchmark: Simple SELECT (no JOIN)
struct SimpleSelectResult {
    int    row_count;
    double total_time_ms;
    double time_per_row_us;
    double throughput_rows_per_sec;
};

SimpleSelectResult benchmark_simple_select(int iterations) {
    QuerySet<Message> message_qs;

    Timer timer;
    int   total_rows = 0;

    for (int iter = 0; iter < iterations; ++iter) {
        auto result = message_qs.select();
        if (!result.has_value()) {
            std::cerr << "SELECT failed: " << result.error().message() << std::endl;
            exit(1);
        }
        total_rows += result.value().size();
    }

    double elapsed_ms      = timer.elapsed_ms();
    double time_per_row_us = (elapsed_ms * 1000.0) / total_rows;
    double throughput      = total_rows / (elapsed_ms / 1000.0);

    return {total_rows, elapsed_ms, time_per_row_us, throughput};
}

// Benchmark: Single FK JOIN
struct JoinSelectResult {
    int    row_count;
    double total_time_ms;
    double time_per_row_us;
    double throughput_rows_per_sec;
};

JoinSelectResult benchmark_single_fk_join(int iterations) {
    QuerySet<Message> message_qs;

    Timer timer;
    int   total_rows = 0;

    for (int iter = 0; iter < iterations; ++iter) {
        auto result = message_qs.join<&Message::sender>().select();
        if (!result.has_value()) {
            std::cerr << "Single FK JOIN failed: " << result.error().message() << std::endl;
            exit(1);
        }
        total_rows += result.value().size();
    }

    double elapsed_ms      = timer.elapsed_ms();
    double time_per_row_us = (elapsed_ms * 1000.0) / total_rows;
    double throughput      = total_rows / (elapsed_ms / 1000.0);

    return {total_rows, elapsed_ms, time_per_row_us, throughput};
}

// Benchmark: Multi FK JOIN
JoinSelectResult benchmark_multi_fk_join(int iterations) {
    QuerySet<Message> message_qs;

    Timer timer;
    int   total_rows = 0;

    for (int iter = 0; iter < iterations; ++iter) {
        auto result = message_qs.join<&Message::sender, &Message::receiver>().select();
        if (!result.has_value()) {
            std::cerr << "Multi FK JOIN failed: " << result.error().message() << std::endl;
            exit(1);
        }
        total_rows += result.value().size();
    }

    double elapsed_ms      = timer.elapsed_ms();
    double time_per_row_us = (elapsed_ms * 1000.0) / total_rows;
    double throughput      = total_rows / (elapsed_ms / 1000.0);

    return {total_rows, elapsed_ms, time_per_row_us, throughput};
}

// Print formatted results
void print_header() {
    std::cout << "=== STORM ORM JOIN PERFORMANCE MICRO-BENCHMARK ===" << std::endl;
    std::cout << "Testing virtual method overhead in JOIN operations" << std::endl;
    std::cout << std::endl;
}

void print_benchmark_result(const char* name, const auto& result, const SimpleSelectResult& baseline) {
    std::cout << name << ":" << std::endl;
    std::cout << "  Rows processed: " << result.row_count << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << result.total_time_ms << " ms" << std::endl;
    std::cout << "  Time per row: " << std::fixed << std::setprecision(4) << result.time_per_row_us << " μs"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << result.throughput_rows_per_sec << " rows/sec"
              << std::endl;

    // Calculate overhead vs baseline
    double overhead_pct = ((result.time_per_row_us / baseline.time_per_row_us) - 1.0) * 100.0;
    std::cout << "  Overhead vs simple SELECT: " << std::fixed << std::setprecision(2) << overhead_pct << "%"
              << std::endl;
    std::cout << std::endl;
}

int main(int argc, char* argv[]) {
    // Parse command-line arguments
    int num_users    = 100;
    int num_messages = 1000;
    int iterations   = 10;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.find("--users=") == 0) {
            num_users = std::stoi(arg.substr(8));
        } else if (arg.find("--messages=") == 0) {
            num_messages = std::stoi(arg.substr(11));
        } else if (arg.find("--iterations=") == 0) {
            iterations = std::stoi(arg.substr(13));
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --users=N          Number of users to create (default: 100)" << std::endl;
            std::cout << "  --messages=N       Number of messages to create (default: 1000)" << std::endl;
            std::cout << "  --iterations=N     Number of iterations to run (default: 10)" << std::endl;
            std::cout << "  --help, -h         Show this help message" << std::endl;
            return 0;
        }
    }

    print_header();

    std::cout << "Configuration:" << std::endl;
    std::cout << "  Users: " << num_users << std::endl;
    std::cout << "  Messages: " << num_messages << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << std::endl;

    // Setup database
    std::cout << "Setting up test database..." << std::endl;
    setup_database(num_users, num_messages);
    std::cout << "Database setup complete." << std::endl;
    std::cout << std::endl;

    // Run benchmarks
    std::cout << "=== BENCHMARK RESULTS ===" << std::endl;
    std::cout << std::endl;

    // Baseline: simple SELECT (no JOIN)
    std::cout << "Running simple SELECT (baseline)..." << std::endl;
    auto baseline = benchmark_simple_select(iterations);
    std::cout << "Simple SELECT (baseline):" << std::endl;
    std::cout << "  Rows processed: " << baseline.row_count << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << baseline.total_time_ms << " ms" << std::endl;
    std::cout << "  Time per row: " << std::fixed << std::setprecision(4) << baseline.time_per_row_us << " μs"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << baseline.throughput_rows_per_sec
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    // Single FK JOIN
    std::cout << "Running single FK JOIN..." << std::endl;
    auto single_fk = benchmark_single_fk_join(iterations);
    print_benchmark_result("Single FK JOIN", single_fk, baseline);

    // Multi FK JOIN
    std::cout << "Running multi FK JOIN..." << std::endl;
    auto multi_fk = benchmark_multi_fk_join(iterations);
    print_benchmark_result("Multi FK JOIN", multi_fk, baseline);

    // Summary
    std::cout << "=== SUMMARY ===" << std::endl;
    std::cout << "Virtual method overhead analysis:" << std::endl;

    double single_fk_overhead = ((single_fk.time_per_row_us / baseline.time_per_row_us) - 1.0) * 100.0;
    double multi_fk_overhead  = ((multi_fk.time_per_row_us / baseline.time_per_row_us) - 1.0) * 100.0;

    std::cout << "  Single FK JOIN overhead: " << std::fixed << std::setprecision(2) << single_fk_overhead << "%"
              << std::endl;
    std::cout << "  Multi FK JOIN overhead: " << std::fixed << std::setprecision(2) << multi_fk_overhead << "%"
              << std::endl;
    std::cout << std::endl;

    // Interpretation
    std::cout << "Interpretation:" << std::endl;
    if (single_fk_overhead < 5.0) {
        std::cout << "  ✓ Virtual call overhead is NEGLIGIBLE (<5%)" << std::endl;
    } else if (single_fk_overhead < 10.0) {
        std::cout << "  ~ Virtual call overhead is ACCEPTABLE (5-10%)" << std::endl;
    } else if (single_fk_overhead < 20.0) {
        std::cout << "  ⚠ Virtual call overhead is MODERATE (10-20%)" << std::endl;
    } else {
        std::cout << "  ✗ Virtual call overhead is SIGNIFICANT (>20%)" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "Note: The overhead includes:" << std::endl;
    std::cout << "  - Virtual method dispatch (extract_row)" << std::endl;
    std::cout << "  - Additional SQL generation (JOIN clauses)" << std::endl;
    std::cout << "  - More complex row extraction (multiple tables)" << std::endl;
    std::cout << "  - SQLite INNER JOIN overhead" << std::endl;
    std::cout << std::endl;

    // Cleanup
    QuerySet<User>::clear_default_connection();

    return 0;
}
