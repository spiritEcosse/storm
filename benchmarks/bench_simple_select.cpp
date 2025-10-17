#include <cstring>
#include <iostream>
#include <iomanip>
#include <vector>
#include <sqlite3.h>
#include "common/benchmark_utils.hpp"

import storm;
import <expected>;
import <span>;
import <string>;

using namespace storm;
using namespace storm::benchmark;

// Realistic models with FK relationships
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

// Configuration
struct BenchmarkConfig {
    int num_users = 100;
    int num_messages = 10000;
    int iterations = 100;
};

// Setup database with test data
void setup_database(int num_users, int num_messages) {
    auto result = QuerySet<User>::set_default_connection(":memory:");
    if (!result.has_value()) {
        throw std::runtime_error("Failed to open database");
    }

    auto& conn = QuerySet<User>::get_default_connection();

    // Create tables
    auto create_user = conn.execute(
            "CREATE TABLE User ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL, "
            "age INTEGER NOT NULL"
            ")"
    );
    if (!create_user.has_value()) {
        throw std::runtime_error("Failed to create User table");
    }

    auto create_message = conn.execute(
            "CREATE TABLE Message ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "sender_id INTEGER NOT NULL, "
            "receiver_id INTEGER NOT NULL, "
            "text TEXT NOT NULL"
            ")"
    );
    if (!create_message.has_value()) {
        throw std::runtime_error("Failed to create Message table");
    }

    // Insert users
    QuerySet<User> user_qs;
    std::vector<User> users;
    users.reserve(num_users);
    for (int i = 0; i < num_users; ++i) {
        users.push_back({0, "User" + std::to_string(i), 20 + (i % 50)});
    }
    auto user_result = user_qs.insert(std::span<const User>(users));
    if (!user_result.has_value()) {
        throw std::runtime_error("Failed to insert users");
    }

    const auto& user_ids = user_result.value();

    // Insert messages
    QuerySet<Message> message_qs;
    std::vector<Message> messages;
    messages.reserve(num_messages);
    for (int i = 0; i < num_messages; ++i) {
        int sender_idx = i % user_ids.size();
        int receiver_idx = (i + 1) % user_ids.size();
        messages.push_back({
                0,
                User{static_cast<int>(user_ids[sender_idx]), "", 0},
                User{static_cast<int>(user_ids[receiver_idx]), "", 0},
                "Message " + std::to_string(i)
        });
    }
    auto msg_result = message_qs.insert(std::span<const Message>(messages));
    if (!msg_result.has_value()) {
        throw std::runtime_error("Failed to insert messages");
    }
}

void teardown_database() {
    QuerySet<User>::clear_default_connection();
}

// Global statistics for comparison
struct BenchmarkStats {
    double storm_select_throughput = 0;
    double raw_select_throughput = 0;
} global_stats;

// Benchmark: Storm ORM - Simple SELECT (no LIMIT/OFFSET)
void benchmark_storm_select(const BenchmarkConfig& config) {
    format_utils::print_benchmark_header("Storm ORM - Simple SELECT");
    setup_database(config.num_users, config.num_messages);

    QuerySet<User> user_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int64_t total_rows = 0;

    for (int i = 0; i < config.iterations; ++i) {
        timer.reset();
        auto result = user_qs.select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "Storm ORM - Simple SELECT from " << config.num_users << " users:" << std::endl;
    std::cout << "  Iterations: " << config.iterations << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Avg per query: " << std::fixed << std::setprecision(4)
              << (total_time / config.iterations) << " ms" << std::endl;
    std::cout << "  Total rows: " << total_rows << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (total_rows / (total_time / 1000.0)) / 1000000.0 << " M rows/sec" << std::endl;
    std::cout << std::endl;

    global_stats.storm_select_throughput = total_rows / (total_time / 1000.0);
    teardown_database();
}

// Benchmark: Raw SQLite - Simple SELECT (no LIMIT/OFFSET)
void benchmark_raw_select(const BenchmarkConfig& config) {
    format_utils::print_benchmark_header("Raw SQLite - Simple SELECT");
    setup_database(config.num_users, config.num_messages);

    auto& conn = QuerySet<User>::get_default_connection();
    std::string sql = "SELECT id, name, age FROM User";

    BenchmarkTimer timer;
    double total_time = 0;
    int64_t total_rows = 0;

    for (int i = 0; i < config.iterations; ++i) {
        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto stmt = std::move(stmt_result.value());

        timer.reset();
        std::vector<User> results;
        results.reserve(config.num_users);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                User user;
                user.id = stmt.extract_int(0);
                user.name = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                user.age = stmt.extract_int(2);
                results.push_back(std::move(user));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_rows += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite - Simple SELECT from " << config.num_users << " users:" << std::endl;
    std::cout << "  Iterations: " << config.iterations << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Avg per query: " << std::fixed << std::setprecision(4)
              << (total_time / config.iterations) << " ms" << std::endl;
    std::cout << "  Total rows: " << total_rows << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (total_rows / (total_time / 1000.0)) / 1000000.0 << " M rows/sec" << std::endl;
    std::cout << std::endl;

    global_stats.raw_select_throughput = total_rows / (total_time / 1000.0);
    teardown_database();
}

void print_comparison_summary() {
    std::cout << "======================================" << std::endl;
    std::cout << "=== PERFORMANCE COMPARISON SUMMARY ===" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << std::endl;

    auto calc_efficiency = [](double storm, double raw) -> double {
        return raw > 0 ? (storm / raw) * 100.0 : 0.0;
    };

    std::cout << std::fixed << std::setprecision(2);

    if (global_stats.storm_select_throughput > 0) {
        std::cout << "Simple SELECT Performance:" << std::endl;
        std::cout << "  Storm ORM:   " << global_stats.storm_select_throughput / 1000000.0 << " M rows/sec" << std::endl;
        std::cout << "  Raw SQLite:  " << global_stats.raw_select_throughput / 1000000.0 << " M rows/sec" << std::endl;
        std::cout << "  Efficiency:  " << calc_efficiency(global_stats.storm_select_throughput, global_stats.raw_select_throughput) << "%" << std::endl;
        std::cout << std::endl;
    }

    std::cout << "Key Features:" << std::endl;
    std::cout << "  ✓ Baseline performance measurement with lightweight User struct" << std::endl;
    std::cout << "  ✓ Simple 3-field table (id, name, age)" << std::endl;
    std::cout << "  ✓ No JOIN overhead - pure SELECT efficiency" << std::endl;
    std::cout << "  ✓ Reflects Storm ORM's core compile-time SQL generation" << std::endl;
    std::cout << std::endl;
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --users=N         Number of users (default: 100)" << std::endl;
    std::cout << "  --messages=N      Number of messages (default: 10000)" << std::endl;
    std::cout << "  --iterations=N    Number of iterations (default: 100)" << std::endl;
    std::cout << std::endl;
    std::cout << "  --help, -h        Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << "                        # Run with defaults" << std::endl;
    std::cout << "  " << program_name << " --users=1000          # Test with 1000 users" << std::endl;
    std::cout << "  " << program_name << " --iterations=500      # Run 500 iterations" << std::endl;
}

BenchmarkConfig parse_arguments(int argc, char* argv[]) {
    BenchmarkConfig config;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--users=", 8) == 0) {
            config.num_users = std::stoi(argv[i] + 8);
        } else if (strncmp(argv[i], "--messages=", 11) == 0) {
            config.num_messages = std::stoi(argv[i] + 11);
        } else if (strncmp(argv[i], "--iterations=", 13) == 0) {
            config.iterations = std::stoi(argv[i] + 13);
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

int main(int argc, char* argv[]) {
    BenchmarkConfig config = parse_arguments(argc, argv);

    std::cout << "====================================================" << std::endl;
    std::cout << "      Storm ORM Simple SELECT Performance Benchmark" << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Users:       " << config.num_users << std::endl;
    std::cout << "  Messages:    " << config.num_messages << std::endl;
    std::cout << "  Iterations:  " << config.iterations << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << std::endl;

    // Run benchmarks
    benchmark_storm_select(config);
    benchmark_raw_select(config);

    // Print comparison summary
    print_comparison_summary();

    return 0;
}
