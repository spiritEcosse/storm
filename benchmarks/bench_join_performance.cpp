#include <cstring>
#include <iostream>
#include <iomanip>
#include "benchmark_utils.hpp"

import storm;
import <string>;
import <vector>;
import <span>;
import <expected>;

using namespace storm;
using namespace storm::benchmark;

// Test models for JOIN benchmarks
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

// Benchmark: SELECT without JOIN (baseline)
void benchmark_select_no_join(int num_messages, int iterations = 100) {
    int num_users = std::max(100, num_messages / 10);
    setup_database(num_users, num_messages);

    QuerySet<Message> message_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = message_qs.select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "SELECT (no JOIN) - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: INNER JOIN single FK
void benchmark_inner_join_single_fk(int num_messages, int iterations = 100) {
    int num_users = std::max(100, num_messages / 10);
    setup_database(num_users, num_messages);

    QuerySet<Message> message_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = message_qs.join<&Message::sender>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "INNER JOIN (single FK: sender) - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: INNER JOIN multi FK
void benchmark_inner_join_multi_fk(int num_messages, int iterations = 100) {
    int num_users = std::max(100, num_messages / 10);
    setup_database(num_users, num_messages);

    QuerySet<Message> message_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = message_qs.join<&Message::sender, &Message::receiver>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "INNER JOIN (multi FK: sender + receiver) - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: LEFT JOIN single FK
void benchmark_left_join_single_fk(int num_messages, int iterations = 100) {
    int num_users = std::max(100, num_messages / 10);
    setup_database(num_users, num_messages);

    QuerySet<Message> message_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = message_qs.left_join<&Message::sender>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "LEFT JOIN (single FK: sender) - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: LEFT JOIN multi FK
void benchmark_left_join_multi_fk(int num_messages, int iterations = 100) {
    int num_users = std::max(100, num_messages / 10);
    setup_database(num_users, num_messages);

    QuerySet<Message> message_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = message_qs.left_join<&Message::sender, &Message::receiver>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "LEFT JOIN (multi FK: sender + receiver) - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: RIGHT JOIN single FK
void benchmark_right_join_single_fk(int num_messages, int iterations = 100) {
    int num_users = std::max(100, num_messages / 10);
    setup_database(num_users, num_messages);

    QuerySet<Message> message_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = message_qs.right_join<&Message::sender>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "RIGHT JOIN (single FK: sender) - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: RIGHT JOIN multi FK
void benchmark_right_join_multi_fk(int num_messages, int iterations = 100) {
    int num_users = std::max(100, num_messages / 10);
    setup_database(num_users, num_messages);

    QuerySet<Message> message_qs;
    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = message_qs.right_join<&Message::sender, &Message::receiver>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "RIGHT JOIN (multi FK: sender + receiver) - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite INNER JOIN (for comparison)
void benchmark_raw_sqlite_inner_join(int num_messages, int iterations = 100) {
    int num_users = std::max(100, num_messages / 10);
    setup_database(num_users, num_messages);

    auto& conn = QuerySet<User>::get_default_connection();
    std::string sql =
        "SELECT m.id, m.text, "
        "u1.id, u1.name, u1.age "
        "FROM Message m "
        "INNER JOIN User u1 ON u1.id = m.sender_id";

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto stmt = std::move(stmt_result.value());
        std::vector<Message> results;
        results.reserve(num_messages);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                // Build actual Message object with sender populated
                Message msg;
                msg.id = stmt.extract_int(0);
                msg.text = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));

                // Populate sender FK (fully populated via JOIN)
                msg.sender.id = stmt.extract_int(2);
                msg.sender.name = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(3)));
                msg.sender.age = stmt.extract_int(4);

                // Receiver is not populated (not joined)
                msg.receiver = User{0, "", 0};

                results.push_back(std::move(msg));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_rows += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite INNER JOIN (single FK) - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite INNER JOIN multi FK
void benchmark_raw_sqlite_inner_join_multi(int num_messages, int iterations = 100) {
    int num_users = std::max(100, num_messages / 10);
    setup_database(num_users, num_messages);

    auto& conn = QuerySet<User>::get_default_connection();
    std::string sql =
        "SELECT m.id, m.text, "
        "u1.id, u1.name, u1.age, "
        "u2.id, u2.name, u2.age "
        "FROM Message m "
        "INNER JOIN User u1 ON u1.id = m.sender_id "
        "INNER JOIN User u2 ON u2.id = m.receiver_id";

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto stmt = std::move(stmt_result.value());
        std::vector<Message> results;
        results.reserve(num_messages);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                // Build actual Message object with both sender and receiver populated
                Message msg;
                msg.id = stmt.extract_int(0);
                msg.text = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));

                // Populate sender FK (fully populated via JOIN)
                msg.sender.id = stmt.extract_int(2);
                msg.sender.name = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(3)));
                msg.sender.age = stmt.extract_int(4);

                // Populate receiver FK (fully populated via JOIN)
                msg.receiver.id = stmt.extract_int(5);
                msg.receiver.name = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(6)));
                msg.receiver.age = stmt.extract_int(7);

                results.push_back(std::move(msg));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_rows += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite INNER JOIN (multi FK) - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite LEFT JOIN single FK
void benchmark_raw_sqlite_left_join_single(int num_messages, int iterations = 100) {
    int num_users = std::max(100, num_messages / 10);
    setup_database(num_users, num_messages);

    auto& conn = QuerySet<User>::get_default_connection();
    std::string sql =
        "SELECT m.id, m.text, "
        "u1.id, u1.name, u1.age "
        "FROM Message m "
        "LEFT JOIN User u1 ON u1.id = m.sender_id";

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto stmt = std::move(stmt_result.value());
        std::vector<Message> results;
        results.reserve(num_messages);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                // Build actual Message object with LEFT JOIN handling
                Message msg;
                msg.id = stmt.extract_int(0);
                msg.text = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));

                // Populate sender FK if not NULL (LEFT JOIN may have NULL)
                if (!stmt.is_null(2)) {
                    msg.sender.id = stmt.extract_int(2);
                    msg.sender.name = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(3)));
                    msg.sender.age = stmt.extract_int(4);
                } else {
                    msg.sender = User{0, "", 0};
                }

                // Receiver is not populated (not joined)
                msg.receiver = User{0, "", 0};

                results.push_back(std::move(msg));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_rows += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite LEFT JOIN (single FK) - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite LEFT JOIN multi FK
void benchmark_raw_sqlite_left_join_multi(int num_messages, int iterations = 100) {
    int num_users = std::max(100, num_messages / 10);
    setup_database(num_users, num_messages);

    auto& conn = QuerySet<User>::get_default_connection();
    std::string sql =
        "SELECT m.id, m.text, "
        "u1.id, u1.name, u1.age, "
        "u2.id, u2.name, u2.age "
        "FROM Message m "
        "LEFT JOIN User u1 ON u1.id = m.sender_id "
        "LEFT JOIN User u2 ON u2.id = m.receiver_id";

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto stmt = std::move(stmt_result.value());
        std::vector<Message> results;
        results.reserve(num_messages);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                // Build actual Message object with LEFT JOIN handling for both FKs
                Message msg;
                msg.id = stmt.extract_int(0);
                msg.text = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));

                // Populate sender FK if not NULL (LEFT JOIN may have NULL)
                if (!stmt.is_null(2)) {
                    msg.sender.id = stmt.extract_int(2);
                    msg.sender.name = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(3)));
                    msg.sender.age = stmt.extract_int(4);
                } else {
                    msg.sender = User{0, "", 0};
                }

                // Populate receiver FK if not NULL (LEFT JOIN may have NULL)
                if (!stmt.is_null(5)) {
                    msg.receiver.id = stmt.extract_int(5);
                    msg.receiver.name = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(6)));
                    msg.receiver.age = stmt.extract_int(7);
                } else {
                    msg.receiver = User{0, "", 0};
                }

                results.push_back(std::move(msg));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_rows += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite LEFT JOIN (multi FK) - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite RIGHT JOIN single FK
void benchmark_raw_sqlite_right_join_single(int num_messages, int iterations = 100) {
    int num_users = std::max(100, num_messages / 10);
    setup_database(num_users, num_messages);

    auto& conn = QuerySet<User>::get_default_connection();
    std::string sql =
        "SELECT m.id, m.text, "
        "u1.id, u1.name, u1.age "
        "FROM Message m "
        "RIGHT JOIN User u1 ON u1.id = m.sender_id";

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto stmt = std::move(stmt_result.value());
        std::vector<Message> results;
        results.reserve(num_messages);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                // Build actual Message object with RIGHT JOIN handling
                Message msg;

                // Message fields may be NULL (RIGHT JOIN)
                if (!stmt.is_null(0)) {
                    msg.id = stmt.extract_int(0);
                    msg.text = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                } else {
                    msg.id = 0;
                    msg.text = "";
                }

                // Sender FK is always populated (right table)
                msg.sender.id = stmt.extract_int(2);
                msg.sender.name = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(3)));
                msg.sender.age = stmt.extract_int(4);

                // Receiver is not populated (not joined)
                msg.receiver = User{0, "", 0};

                results.push_back(std::move(msg));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_rows += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite RIGHT JOIN (single FK) - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite RIGHT JOIN multi FK
void benchmark_raw_sqlite_right_join_multi(int num_messages, int iterations = 100) {
    int num_users = std::max(100, num_messages / 10);
    setup_database(num_users, num_messages);

    auto& conn = QuerySet<User>::get_default_connection();
    std::string sql =
        "SELECT m.id, m.text, "
        "u1.id, u1.name, u1.age, "
        "u2.id, u2.name, u2.age "
        "FROM Message m "
        "RIGHT JOIN User u1 ON u1.id = m.sender_id "
        "RIGHT JOIN User u2 ON u2.id = m.receiver_id";

    BenchmarkTimer timer;
    double total_time = 0;
    int total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto stmt = std::move(stmt_result.value());
        std::vector<Message> results;
        results.reserve(num_messages);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                // Build actual Message object with RIGHT JOIN handling for both FKs
                Message msg;

                // Message fields may be NULL (RIGHT JOIN)
                if (!stmt.is_null(0)) {
                    msg.id = stmt.extract_int(0);
                    msg.text = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                } else {
                    msg.id = 0;
                    msg.text = "";
                }

                // Sender FK may be NULL (right table on second JOIN)
                if (!stmt.is_null(2)) {
                    msg.sender.id = stmt.extract_int(2);
                    msg.sender.name = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(3)));
                    msg.sender.age = stmt.extract_int(4);
                } else {
                    msg.sender = User{0, "", 0};
                }

                // Receiver FK may be NULL (right table on second JOIN)
                if (!stmt.is_null(5)) {
                    msg.receiver.id = stmt.extract_int(5);
                    msg.receiver.name = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(6)));
                    msg.receiver.age = stmt.extract_int(7);
                } else {
                    msg.receiver = User{0, "", 0};
                }

                results.push_back(std::move(msg));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_rows += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite RIGHT JOIN (multi FK) - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4)
              << (total_time / iterations) << " ms" << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0)
              << (total_rows / (total_time / 1000.0)) << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [--size=N] [--iterations=N]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --size=N          Number of messages (default: 1000)" << std::endl;
    std::cout << "  --iterations=N    Number of iterations (default: 100)" << std::endl;
    std::cout << "  --help, -h        Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    std::vector<int> test_sizes = {100, 1000, 10000};
    int iterations = 100;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--size=", 7) == 0) {
            test_sizes.clear();
            test_sizes.push_back(std::stoi(argv[i] + 7));
        } else if (strncmp(argv[i], "--iterations=", 13) == 0) {
            iterations = std::stoi(argv[i] + 13);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    std::cout << "====================================================" << std::endl;
    std::cout << "      Storm ORM JOIN Performance Benchmark" << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << std::endl;

    for (int size : test_sizes) {
        std::cout << "Testing with " << size << " messages (iterations: " << iterations << ")" << std::endl;
        std::cout << std::string(60, '-') << std::endl;
        std::cout << std::endl;

        benchmark_select_no_join(size, iterations);

        std::cout << "--- Storm ORM JOINs ---" << std::endl;
        benchmark_inner_join_single_fk(size, iterations);
        benchmark_inner_join_multi_fk(size, iterations);
        benchmark_left_join_single_fk(size, iterations);
        benchmark_left_join_multi_fk(size, iterations);
        benchmark_right_join_single_fk(size, iterations);
        benchmark_right_join_multi_fk(size, iterations);

        std::cout << "--- Raw SQLite JOINs ---" << std::endl;
        benchmark_raw_sqlite_inner_join(size, iterations);
        benchmark_raw_sqlite_inner_join_multi(size, iterations);
        benchmark_raw_sqlite_left_join_single(size, iterations);
        benchmark_raw_sqlite_left_join_multi(size, iterations);
        benchmark_raw_sqlite_right_join_single(size, iterations);
        benchmark_raw_sqlite_right_join_multi(size, iterations);

        std::cout << std::string(60, '=') << std::endl;
        std::cout << std::endl;
    }

    return 0;
}
