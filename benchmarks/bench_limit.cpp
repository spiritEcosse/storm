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

// Realistic models with FK relationships (like bench_join.cpp)
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
    int limit_size = 100;
    int offset_size = 5000;
    int iterations = 100;
    bool run_all = true;
    bool run_storm_limit = false;
    bool run_storm_limit_offset = false;
    bool run_storm_offset = false;
    bool run_storm_join_limit = false;
    bool run_raw_limit = false;
    bool run_raw_limit_offset = false;
    bool run_raw_offset = false;
    bool run_raw_join_limit = false;
};

// Setup database with test data for simple LIMIT/OFFSET tests (User table only)
void setup_simple_database(int num_rows) {
    auto result = QuerySet<User>::set_default_connection(":memory:");
    if (!result.has_value()) {
        throw std::runtime_error("Failed to open database");
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
        throw std::runtime_error("Failed to create User table");
    }

    // Insert users
    QuerySet<User> user_qs;
    std::vector<User> users;
    users.reserve(num_rows);
    for (int i = 0; i < num_rows; ++i) {
        users.push_back({0, "User" + std::to_string(i), 20 + (i % 50)});
    }
    auto user_result = user_qs.insert(std::span<const User>(users));
    if (!user_result.has_value()) {
        throw std::runtime_error("Failed to insert users");
    }
}

// Setup database with FK relationships for JOIN tests (User + Message tables)
void setup_join_database(int num_users, int num_messages) {
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
    double storm_limit_throughput = 0;
    double storm_limit_offset_throughput = 0;
    double storm_offset_throughput = 0;
    double storm_join_limit_throughput = 0;
    double raw_limit_throughput = 0;
    double raw_limit_offset_throughput = 0;
    double raw_offset_throughput = 0;
    double raw_join_limit_throughput = 0;
} global_stats;

// Benchmark: Storm ORM - LIMIT only
void benchmark_storm_limit(const BenchmarkConfig& config) {
    format_utils::print_benchmark_header("Storm ORM - SELECT with LIMIT");
    setup_simple_database(config.num_messages);

    QuerySet<User> user_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int64_t total_rows = 0;

    for (int i = 0; i < config.iterations; ++i) {
        timer.reset();
        auto result = user_qs.limit(config.limit_size).select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "Storm ORM - SELECT with LIMIT " << config.limit_size
              << " from " << config.num_messages << " users:" << std::endl;
    std::cout << "  Iterations: " << config.iterations << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Avg per query: " << std::fixed << std::setprecision(4)
              << (total_time / config.iterations) << " ms" << std::endl;
    std::cout << "  Total rows: " << total_rows << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (total_rows / (total_time / 1000.0)) / 1000000.0 << " M rows/sec" << std::endl;
    std::cout << std::endl;

    global_stats.storm_limit_throughput = total_rows / (total_time / 1000.0);
    teardown_database();
}

// Benchmark: Storm ORM - LIMIT + OFFSET
void benchmark_storm_limit_offset(const BenchmarkConfig& config) {
    format_utils::print_benchmark_header("Storm ORM - SELECT with LIMIT + OFFSET");
    setup_simple_database(config.num_messages);

    QuerySet<User> user_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int64_t total_rows = 0;

    for (int i = 0; i < config.iterations; ++i) {
        timer.reset();
        auto result = user_qs.limit(config.limit_size).offset(config.offset_size).select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "Storm ORM - SELECT with LIMIT " << config.limit_size
              << " OFFSET " << config.offset_size << " from " << config.num_messages << " users:" << std::endl;
    std::cout << "  Iterations: " << config.iterations << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Avg per query: " << std::fixed << std::setprecision(4)
              << (total_time / config.iterations) << " ms" << std::endl;
    std::cout << "  Total rows: " << total_rows << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (total_rows / (total_time / 1000.0)) / 1000000.0 << " M rows/sec" << std::endl;
    std::cout << std::endl;

    global_stats.storm_limit_offset_throughput = total_rows / (total_time / 1000.0);
    teardown_database();
}

// Benchmark: Storm ORM - OFFSET only
void benchmark_storm_offset(const BenchmarkConfig& config) {
    format_utils::print_benchmark_header("Storm ORM - SELECT with OFFSET only");
    setup_simple_database(config.num_messages);

    QuerySet<User> user_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int64_t total_rows = 0;

    for (int i = 0; i < config.iterations; ++i) {
        timer.reset();
        auto result = user_qs.offset(config.offset_size).select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "Storm ORM - SELECT with OFFSET " << config.offset_size
              << " from " << config.num_messages << " users:" << std::endl;
    std::cout << "  Iterations: " << config.iterations << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Avg per query: " << std::fixed << std::setprecision(4)
              << (total_time / config.iterations) << " ms" << std::endl;
    std::cout << "  Total rows: " << total_rows << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (total_rows / (total_time / 1000.0)) / 1000000.0 << " M rows/sec" << std::endl;
    std::cout << std::endl;

    global_stats.storm_offset_throughput = total_rows / (total_time / 1000.0);
    teardown_database();
}

// Benchmark: Storm ORM - JOIN + LIMIT + OFFSET
void benchmark_storm_join_limit(const BenchmarkConfig& config) {
    format_utils::print_benchmark_header("Storm ORM - JOIN + LIMIT + OFFSET");
    setup_join_database(config.num_users, config.num_messages);

    QuerySet<Message> message_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int64_t total_rows = 0;

    for (int i = 0; i < config.iterations; ++i) {
        timer.reset();
        auto result = message_qs.join<&Message::sender, &Message::receiver>()
                                 .limit(config.limit_size)
                                 .offset(config.offset_size)
                                 .select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "Storm ORM - JOIN (sender+receiver) with LIMIT " << config.limit_size
              << " OFFSET " << config.offset_size << " from " << config.num_messages << " messages:" << std::endl;
    std::cout << "  Iterations: " << config.iterations << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Avg per query: " << std::fixed << std::setprecision(4)
              << (total_time / config.iterations) << " ms" << std::endl;
    std::cout << "  Total rows: " << total_rows << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (total_rows / (total_time / 1000.0)) / 1000000.0 << " M rows/sec" << std::endl;
    std::cout << std::endl;

    global_stats.storm_join_limit_throughput = total_rows / (total_time / 1000.0);
    teardown_database();
}

// Benchmark: Raw SQLite - LIMIT only
void benchmark_raw_limit(const BenchmarkConfig& config) {
    format_utils::print_benchmark_header("Raw SQLite - SELECT with LIMIT");
    setup_simple_database(config.num_messages);

    auto& conn = QuerySet<User>::get_default_connection();
    std::string sql = "SELECT id, name, age FROM User LIMIT ?";

    // Prepare statement ONCE (cached, like Storm ORM does)
    auto stmt_result = conn.prepare_cached(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_database();
        return;
    }
    auto* stmt = *stmt_result;

    BenchmarkTimer timer;
    double total_time = 0;
    int64_t total_rows = 0;

    using StmtType = std::remove_reference_t<decltype(*stmt)>;

    for (int i = 0; i < config.iterations; ++i) {
        (void)stmt->bind_int(1, config.limit_size);

        timer.reset();
        std::vector<User> results;
        results.reserve(config.limit_size);

        while (true) {
            int step = stmt->step_raw();
            if (step == StmtType::ROW_AVAILABLE) {
                User user;
                user.id = stmt->extract_int(0);
                user.name = std::string(reinterpret_cast<const char*>(stmt->extract_text_ptr(1)));
                user.age = stmt->extract_int(2);
                results.push_back(std::move(user));
            } else if (step == StmtType::NO_MORE_ROWS) {
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_rows += results.size();
        total_time += elapsed;
        stmt->reset();
    }

    std::cout << "Raw SQLite - SELECT with LIMIT " << config.limit_size
              << " from " << config.num_messages << " users:" << std::endl;
    std::cout << "  Iterations: " << config.iterations << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Avg per query: " << std::fixed << std::setprecision(4)
              << (total_time / config.iterations) << " ms" << std::endl;
    std::cout << "  Total rows: " << total_rows << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (total_rows / (total_time / 1000.0)) / 1000000.0 << " M rows/sec" << std::endl;
    std::cout << std::endl;

    global_stats.raw_limit_throughput = total_rows / (total_time / 1000.0);
    teardown_database();
}

// Benchmark: Raw SQLite - LIMIT + OFFSET
void benchmark_raw_limit_offset(const BenchmarkConfig& config) {
    format_utils::print_benchmark_header("Raw SQLite - SELECT with LIMIT + OFFSET");
    setup_simple_database(config.num_messages);

    auto& conn = QuerySet<User>::get_default_connection();
    std::string sql = "SELECT id, name, age FROM User LIMIT ? OFFSET ?";

    // Prepare statement ONCE (cached, like Storm ORM does)
    auto stmt_result = conn.prepare_cached(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_database();
        return;
    }
    auto* stmt = *stmt_result;

    BenchmarkTimer timer;
    double total_time = 0;
    int64_t total_rows = 0;

    using StmtType = std::remove_reference_t<decltype(*stmt)>;

    for (int i = 0; i < config.iterations; ++i) {
        (void)stmt->bind_int(1, config.limit_size);
        (void)stmt->bind_int(2, config.offset_size);

        timer.reset();
        std::vector<User> results;
        results.reserve(config.limit_size);

        while (true) {
            int step = stmt->step_raw();
            if (step == StmtType::ROW_AVAILABLE) {
                User user;
                user.id = stmt->extract_int(0);
                user.name = std::string(reinterpret_cast<const char*>(stmt->extract_text_ptr(1)));
                user.age = stmt->extract_int(2);
                results.push_back(std::move(user));
            } else if (step == StmtType::NO_MORE_ROWS) {
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_rows += results.size();
        total_time += elapsed;
        stmt->reset();
    }

    std::cout << "Raw SQLite - SELECT with LIMIT " << config.limit_size
              << " OFFSET " << config.offset_size << " from " << config.num_messages << " users:" << std::endl;
    std::cout << "  Iterations: " << config.iterations << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Avg per query: " << std::fixed << std::setprecision(4)
              << (total_time / config.iterations) << " ms" << std::endl;
    std::cout << "  Total rows: " << total_rows << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (total_rows / (total_time / 1000.0)) / 1000000.0 << " M rows/sec" << std::endl;
    std::cout << std::endl;

    global_stats.raw_limit_offset_throughput = total_rows / (total_time / 1000.0);
    teardown_database();
}

// Benchmark: Raw SQLite - OFFSET only
void benchmark_raw_offset(const BenchmarkConfig& config) {
    format_utils::print_benchmark_header("Raw SQLite - SELECT with OFFSET only");
    setup_simple_database(config.num_messages);

    auto& conn = QuerySet<User>::get_default_connection();
    std::string sql = "SELECT id, name, age FROM User LIMIT ? OFFSET ?";

    // Prepare statement ONCE (cached, like Storm ORM does)
    auto stmt_result = conn.prepare_cached(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_database();
        return;
    }
    auto* stmt = *stmt_result;

    BenchmarkTimer timer;
    double total_time = 0;
    int64_t total_rows = 0;

    using StmtType = std::remove_reference_t<decltype(*stmt)>;

    for (int i = 0; i < config.iterations; ++i) {
        (void)stmt->bind_int(1, -1);  // -1 means unlimited
        (void)stmt->bind_int(2, config.offset_size);

        timer.reset();
        std::vector<User> results;
        // Avoid underflow when offset >= num_messages
        size_t expected_rows = (config.offset_size < config.num_messages)
                                ? (config.num_messages - config.offset_size)
                                : 0;
        results.reserve(expected_rows);

        while (true) {
            int step = stmt->step_raw();
            if (step == StmtType::ROW_AVAILABLE) {
                User user;
                user.id = stmt->extract_int(0);
                user.name = std::string(reinterpret_cast<const char*>(stmt->extract_text_ptr(1)));
                user.age = stmt->extract_int(2);
                results.push_back(std::move(user));
            } else if (step == StmtType::NO_MORE_ROWS) {
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_rows += results.size();
        total_time += elapsed;
        stmt->reset();
    }

    std::cout << "Raw SQLite - SELECT with OFFSET " << config.offset_size
              << " from " << config.num_messages << " users:" << std::endl;
    std::cout << "  Iterations: " << config.iterations << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Avg per query: " << std::fixed << std::setprecision(4)
              << (total_time / config.iterations) << " ms" << std::endl;
    std::cout << "  Total rows: " << total_rows << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (total_rows / (total_time / 1000.0)) / 1000000.0 << " M rows/sec" << std::endl;
    std::cout << std::endl;

    global_stats.raw_offset_throughput = total_rows / (total_time / 1000.0);
    teardown_database();
}

// Benchmark: Raw SQLite - JOIN + LIMIT + OFFSET
void benchmark_raw_join_limit(const BenchmarkConfig& config) {
    format_utils::print_benchmark_header("Raw SQLite - JOIN + LIMIT + OFFSET");
    setup_join_database(config.num_users, config.num_messages);

    auto& conn = QuerySet<User>::get_default_connection();
    std::string sql =
        "SELECT m.id, m.text, "
        "u1.id, u1.name, u1.age, "
        "u2.id, u2.name, u2.age "
        "FROM Message m "
        "INNER JOIN User u1 ON u1.id = m.sender_id "
        "INNER JOIN User u2 ON u2.id = m.receiver_id "
        "LIMIT ? OFFSET ?";

    // Prepare statement ONCE (cached, like Storm ORM does)
    auto stmt_result = conn.prepare_cached(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_database();
        return;
    }
    auto* stmt = *stmt_result;

    BenchmarkTimer timer;
    double total_time = 0;
    int64_t total_rows = 0;

    using StmtType = std::remove_reference_t<decltype(*stmt)>;

    for (int i = 0; i < config.iterations; ++i) {
        (void)stmt->bind_int(1, config.limit_size);
        (void)stmt->bind_int(2, config.offset_size);

        timer.reset();
        std::vector<Message> results;
        results.reserve(config.limit_size);

        while (true) {
            int step = stmt->step_raw();
            if (step == StmtType::ROW_AVAILABLE) {
                Message msg;
                msg.id = stmt->extract_int(0);
                msg.text = std::string(reinterpret_cast<const char*>(stmt->extract_text_ptr(1)));

                // Populate sender
                msg.sender.id = stmt->extract_int(2);
                msg.sender.name = std::string(reinterpret_cast<const char*>(stmt->extract_text_ptr(3)));
                msg.sender.age = stmt->extract_int(4);

                // Populate receiver
                msg.receiver.id = stmt->extract_int(5);
                msg.receiver.name = std::string(reinterpret_cast<const char*>(stmt->extract_text_ptr(6)));
                msg.receiver.age = stmt->extract_int(7);

                results.push_back(std::move(msg));
            } else if (step == StmtType::NO_MORE_ROWS) {
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_rows += results.size();
        total_time += elapsed;
        stmt->reset();
    }

    std::cout << "Raw SQLite - JOIN (sender+receiver) with LIMIT " << config.limit_size
              << " OFFSET " << config.offset_size << " from " << config.num_messages << " messages:" << std::endl;
    std::cout << "  Iterations: " << config.iterations << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Avg per query: " << std::fixed << std::setprecision(4)
              << (total_time / config.iterations) << " ms" << std::endl;
    std::cout << "  Total rows: " << total_rows << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (total_rows / (total_time / 1000.0)) / 1000000.0 << " M rows/sec" << std::endl;
    std::cout << std::endl;

    global_stats.raw_join_limit_throughput = total_rows / (total_time / 1000.0);
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

    if (global_stats.storm_limit_throughput > 0) {
        std::cout << "LIMIT Performance:" << std::endl;
        std::cout << "  Storm ORM:   " << global_stats.storm_limit_throughput / 1000000.0 << " M rows/sec" << std::endl;
        std::cout << "  Raw SQLite:  " << global_stats.raw_limit_throughput / 1000000.0 << " M rows/sec" << std::endl;
        std::cout << "  Efficiency:  " << calc_efficiency(global_stats.storm_limit_throughput, global_stats.raw_limit_throughput) << "%" << std::endl;
        std::cout << std::endl;
    }

    if (global_stats.storm_limit_offset_throughput > 0) {
        std::cout << "LIMIT + OFFSET Performance:" << std::endl;
        std::cout << "  Storm ORM:   " << global_stats.storm_limit_offset_throughput / 1000000.0 << " M rows/sec" << std::endl;
        std::cout << "  Raw SQLite:  " << global_stats.raw_limit_offset_throughput / 1000000.0 << " M rows/sec" << std::endl;
        std::cout << "  Efficiency:  " << calc_efficiency(global_stats.storm_limit_offset_throughput, global_stats.raw_limit_offset_throughput) << "%" << std::endl;
        std::cout << std::endl;
    }

    if (global_stats.storm_offset_throughput > 0) {
        std::cout << "OFFSET Only Performance:" << std::endl;
        std::cout << "  Storm ORM:   " << global_stats.storm_offset_throughput / 1000000.0 << " M rows/sec" << std::endl;
        std::cout << "  Raw SQLite:  " << global_stats.raw_offset_throughput / 1000000.0 << " M rows/sec" << std::endl;
        std::cout << "  Efficiency:  " << calc_efficiency(global_stats.storm_offset_throughput, global_stats.raw_offset_throughput) << "%" << std::endl;
        std::cout << std::endl;
    }

    if (global_stats.storm_join_limit_throughput > 0) {
        std::cout << "JOIN + LIMIT + OFFSET Performance:" << std::endl;
        std::cout << "  Storm ORM:   " << global_stats.storm_join_limit_throughput / 1000000.0 << " M rows/sec" << std::endl;
        std::cout << "  Raw SQLite:  " << global_stats.raw_join_limit_throughput / 1000000.0 << " M rows/sec" << std::endl;
        std::cout << "  Efficiency:  " << calc_efficiency(global_stats.storm_join_limit_throughput, global_stats.raw_join_limit_throughput) << "%" << std::endl;
        std::cout << std::endl;
    }

    // Calculate average efficiency
    int count = 0;
    double sum_efficiency = 0;
    if (global_stats.storm_limit_throughput > 0) {
        sum_efficiency += calc_efficiency(global_stats.storm_limit_throughput, global_stats.raw_limit_throughput);
        count++;
    }
    if (global_stats.storm_limit_offset_throughput > 0) {
        sum_efficiency += calc_efficiency(global_stats.storm_limit_offset_throughput, global_stats.raw_limit_offset_throughput);
        count++;
    }
    if (global_stats.storm_offset_throughput > 0) {
        sum_efficiency += calc_efficiency(global_stats.storm_offset_throughput, global_stats.raw_offset_throughput);
        count++;
    }
    if (global_stats.storm_join_limit_throughput > 0) {
        sum_efficiency += calc_efficiency(global_stats.storm_join_limit_throughput, global_stats.raw_join_limit_throughput);
        count++;
    }

    if (count > 0) {
        std::cout << "Average Efficiency: " << (sum_efficiency / count) << "%" << std::endl;
        std::cout << std::endl;
    }

    std::cout << "Key Features:" << std::endl;
    std::cout << "  ✓ Statement caching with bind parameters" << std::endl;
    std::cout << "  ✓ Three separate caches (no LIMIT, LIMIT only, LIMIT+OFFSET)" << std::endl;
    std::cout << "  ✓ Smart pre-allocation based on LIMIT size" << std::endl;
    std::cout << "  ✓ Works seamlessly with JOIN operations" << std::endl;
    std::cout << "  ✓ Simple User model for basic tests, Message with FKs for JOIN tests" << std::endl;
    std::cout << std::endl;
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --users=N         Number of users (default: 100)" << std::endl;
    std::cout << "  --messages=N      Number of messages (default: 10000)" << std::endl;
    std::cout << "  --limit=N         LIMIT size (default: 100)" << std::endl;
    std::cout << "  --offset=N        OFFSET size (default: 5000)" << std::endl;
    std::cout << "  --iterations=N    Number of iterations (default: 100)" << std::endl;
    std::cout << std::endl;
    std::cout << "Benchmark Selection (run all if none specified):" << std::endl;
    std::cout << "  --storm-limit         Run Storm ORM LIMIT benchmark" << std::endl;
    std::cout << "  --storm-limit-offset  Run Storm ORM LIMIT+OFFSET benchmark" << std::endl;
    std::cout << "  --storm-offset        Run Storm ORM OFFSET benchmark" << std::endl;
    std::cout << "  --storm-join-limit    Run Storm ORM JOIN+LIMIT+OFFSET benchmark" << std::endl;
    std::cout << "  --raw-limit           Run Raw SQLite LIMIT benchmark" << std::endl;
    std::cout << "  --raw-limit-offset    Run Raw SQLite LIMIT+OFFSET benchmark" << std::endl;
    std::cout << "  --raw-offset          Run Raw SQLite OFFSET benchmark" << std::endl;
    std::cout << "  --raw-join-limit      Run Raw SQLite JOIN+LIMIT+OFFSET benchmark" << std::endl;
    std::cout << std::endl;
    std::cout << "  --help, -h        Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << "                                    # Run all benchmarks" << std::endl;
    std::cout << "  " << program_name << " --messages=50000 --limit=500      # Test with larger dataset" << std::endl;
    std::cout << "  " << program_name << " --storm-limit --raw-limit         # Compare LIMIT only" << std::endl;
    std::cout << "  " << program_name << " --storm-join-limit                # Test JOIN with pagination" << std::endl;
}

BenchmarkConfig parse_arguments(int argc, char* argv[]) {
    BenchmarkConfig config;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--users=", 8) == 0) {
            config.num_users = std::stoi(argv[i] + 8);
        } else if (strncmp(argv[i], "--messages=", 11) == 0) {
            config.num_messages = std::stoi(argv[i] + 11);
        } else if (strncmp(argv[i], "--limit=", 8) == 0) {
            config.limit_size = std::stoi(argv[i] + 8);
        } else if (strncmp(argv[i], "--offset=", 9) == 0) {
            config.offset_size = std::stoi(argv[i] + 9);
        } else if (strncmp(argv[i], "--iterations=", 13) == 0) {
            config.iterations = std::stoi(argv[i] + 13);
        } else if (strcmp(argv[i], "--storm-limit") == 0) {
            config.run_storm_limit = true;
            config.run_all = false;
        } else if (strcmp(argv[i], "--storm-limit-offset") == 0) {
            config.run_storm_limit_offset = true;
            config.run_all = false;
        } else if (strcmp(argv[i], "--storm-offset") == 0) {
            config.run_storm_offset = true;
            config.run_all = false;
        } else if (strcmp(argv[i], "--storm-join-limit") == 0) {
            config.run_storm_join_limit = true;
            config.run_all = false;
        } else if (strcmp(argv[i], "--raw-limit") == 0) {
            config.run_raw_limit = true;
            config.run_all = false;
        } else if (strcmp(argv[i], "--raw-limit-offset") == 0) {
            config.run_raw_limit_offset = true;
            config.run_all = false;
        } else if (strcmp(argv[i], "--raw-offset") == 0) {
            config.run_raw_offset = true;
            config.run_all = false;
        } else if (strcmp(argv[i], "--raw-join-limit") == 0) {
            config.run_raw_join_limit = true;
            config.run_all = false;
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
    std::cout << "      Storm ORM LIMIT/OFFSET Performance Benchmark" << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Users:       " << config.num_users << std::endl;
    std::cout << "  Messages:    " << config.num_messages << std::endl;
    std::cout << "  LIMIT size:  " << config.limit_size << " rows" << std::endl;
    std::cout << "  OFFSET size: " << config.offset_size << " rows" << std::endl;
    std::cout << "  Iterations:  " << config.iterations << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << std::endl;

    // Run selected benchmarks
    if (config.run_all || config.run_storm_limit) {
        benchmark_storm_limit(config);
    }
    if (config.run_all || config.run_storm_limit_offset) {
        benchmark_storm_limit_offset(config);
    }
    if (config.run_all || config.run_storm_offset) {
        benchmark_storm_offset(config);
    }
    if (config.run_all || config.run_storm_join_limit) {
        benchmark_storm_join_limit(config);
    }

    if (config.run_all || config.run_raw_limit) {
        benchmark_raw_limit(config);
    }
    if (config.run_all || config.run_raw_limit_offset) {
        benchmark_raw_limit_offset(config);
    }
    if (config.run_all || config.run_raw_offset) {
        benchmark_raw_offset(config);
    }
    if (config.run_all || config.run_raw_join_limit) {
        benchmark_raw_join_limit(config);
    }

    // Print comparison summary
    print_comparison_summary();

    return 0;
}
