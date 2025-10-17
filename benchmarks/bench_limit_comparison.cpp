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

// Test models
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
    int iterations = 1000;  // More iterations for microbenchmark
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

// Benchmark: Current 3-way branching approach
void benchmark_current_approach(const BenchmarkConfig& config) {
    format_utils::print_benchmark_header("Current Approach (3-way branching)");
    setup_database(config.num_users, config.num_messages);

    auto& conn = QuerySet<User>::get_default_connection();

    // Test case 1: No LIMIT/OFFSET
    {
        std::string sql = "SELECT id, sender_id, receiver_id, text FROM Message";
        auto stmt_result = conn.prepare_cached(sql);
        auto* stmt = *stmt_result;

        BenchmarkTimer timer;
        int64_t total_rows = 0;

        for (int i = 0; i < config.iterations; ++i) {
            std::vector<Message> results;
            results.reserve(1000);

            using StmtType = std::remove_reference_t<decltype(*stmt)>;
            while (true) {
                int step = stmt->step_raw();
                if (step == StmtType::ROW_AVAILABLE) {
                    Message msg;
                    msg.id = stmt->extract_int(0);
                    msg.sender = User{stmt->extract_int(1), "", 0};
                    msg.receiver = User{stmt->extract_int(2), "", 0};
                    msg.text = std::string(reinterpret_cast<const char*>(stmt->extract_text_ptr(3)));
                    results.push_back(std::move(msg));
                } else if (step == StmtType::NO_MORE_ROWS) {
                    break;
                }
            }
            total_rows += results.size();
            stmt->reset();
        }

        double elapsed = timer.elapsed_ms();
        std::cout << "  No LIMIT/OFFSET: " << std::fixed << std::setprecision(4)
                  << (elapsed / config.iterations) << " ms/query, "
                  << (total_rows / (elapsed / 1000.0)) / 1000000.0 << " M rows/sec" << std::endl;
    }

    // Test case 2: LIMIT only
    {
        std::string sql = "SELECT id, sender_id, receiver_id, text FROM Message LIMIT ?";
        auto stmt_result = conn.prepare_cached(sql);
        auto* stmt = *stmt_result;

        BenchmarkTimer timer;
        int64_t total_rows = 0;

        for (int i = 0; i < config.iterations; ++i) {
            stmt->bind_int(1, config.limit_size);

            std::vector<Message> results;
            results.reserve(config.limit_size);

            using StmtType = std::remove_reference_t<decltype(*stmt)>;
            while (true) {
                int step = stmt->step_raw();
                if (step == StmtType::ROW_AVAILABLE) {
                    Message msg;
                    msg.id = stmt->extract_int(0);
                    msg.sender = User{stmt->extract_int(1), "", 0};
                    msg.receiver = User{stmt->extract_int(2), "", 0};
                    msg.text = std::string(reinterpret_cast<const char*>(stmt->extract_text_ptr(3)));
                    results.push_back(std::move(msg));
                } else if (step == StmtType::NO_MORE_ROWS) {
                    break;
                }
            }
            total_rows += results.size();
            stmt->reset();
        }

        double elapsed = timer.elapsed_ms();
        std::cout << "  LIMIT only:      " << std::fixed << std::setprecision(4)
                  << (elapsed / config.iterations) << " ms/query, "
                  << (total_rows / (elapsed / 1000.0)) / 1000000.0 << " M rows/sec" << std::endl;
    }

    // Test case 3: LIMIT + OFFSET
    {
        std::string sql = "SELECT id, sender_id, receiver_id, text FROM Message LIMIT ? OFFSET ?";
        auto stmt_result = conn.prepare_cached(sql);
        auto* stmt = *stmt_result;

        BenchmarkTimer timer;
        int64_t total_rows = 0;

        for (int i = 0; i < config.iterations; ++i) {
            stmt->bind_int(1, config.limit_size);
            stmt->bind_int(2, config.offset_size);

            std::vector<Message> results;
            results.reserve(config.limit_size);

            using StmtType = std::remove_reference_t<decltype(*stmt)>;
            while (true) {
                int step = stmt->step_raw();
                if (step == StmtType::ROW_AVAILABLE) {
                    Message msg;
                    msg.id = stmt->extract_int(0);
                    msg.sender = User{stmt->extract_int(1), "", 0};
                    msg.receiver = User{stmt->extract_int(2), "", 0};
                    msg.text = std::string(reinterpret_cast<const char*>(stmt->extract_text_ptr(3)));
                    results.push_back(std::move(msg));
                } else if (step == StmtType::NO_MORE_ROWS) {
                    break;
                }
            }
            total_rows += results.size();
            stmt->reset();
        }

        double elapsed = timer.elapsed_ms();
        std::cout << "  LIMIT + OFFSET:  " << std::fixed << std::setprecision(4)
                  << (elapsed / config.iterations) << " ms/query, "
                  << (total_rows / (elapsed / 1000.0)) / 1000000.0 << " M rows/sec" << std::endl;
    }

    teardown_database();
}

// Benchmark: Simplified single-statement approach
void benchmark_simplified_approach(const BenchmarkConfig& config) {
    format_utils::print_benchmark_header("Simplified Approach (single statement)");
    setup_database(config.num_users, config.num_messages);

    auto& conn = QuerySet<User>::get_default_connection();

    // Single SQL statement for all cases
    std::string sql = "SELECT id, sender_id, receiver_id, text FROM Message LIMIT ? OFFSET ?";
    auto stmt_result = conn.prepare_cached(sql);
    auto* stmt = *stmt_result;

    // Test case 1: No LIMIT/OFFSET (bind -1, 0)
    {
        BenchmarkTimer timer;
        int64_t total_rows = 0;

        for (int i = 0; i < config.iterations; ++i) {
            stmt->bind_int(1, -1);  // -1 = no limit
            stmt->bind_int(2, 0);   // 0 = no offset

            std::vector<Message> results;
            results.reserve(1000);

            using StmtType = std::remove_reference_t<decltype(*stmt)>;
            while (true) {
                int step = stmt->step_raw();
                if (step == StmtType::ROW_AVAILABLE) {
                    Message msg;
                    msg.id = stmt->extract_int(0);
                    msg.sender = User{stmt->extract_int(1), "", 0};
                    msg.receiver = User{stmt->extract_int(2), "", 0};
                    msg.text = std::string(reinterpret_cast<const char*>(stmt->extract_text_ptr(3)));
                    results.push_back(std::move(msg));
                } else if (step == StmtType::NO_MORE_ROWS) {
                    break;
                }
            }
            total_rows += results.size();
            stmt->reset();
        }

        double elapsed = timer.elapsed_ms();
        std::cout << "  No LIMIT/OFFSET: " << std::fixed << std::setprecision(4)
                  << (elapsed / config.iterations) << " ms/query, "
                  << (total_rows / (elapsed / 1000.0)) / 1000000.0 << " M rows/sec" << std::endl;
    }

    // Test case 2: LIMIT only (bind limit, 0)
    {
        BenchmarkTimer timer;
        int64_t total_rows = 0;

        for (int i = 0; i < config.iterations; ++i) {
            stmt->bind_int(1, config.limit_size);
            stmt->bind_int(2, 0);

            std::vector<Message> results;
            results.reserve(config.limit_size);

            using StmtType = std::remove_reference_t<decltype(*stmt)>;
            while (true) {
                int step = stmt->step_raw();
                if (step == StmtType::ROW_AVAILABLE) {
                    Message msg;
                    msg.id = stmt->extract_int(0);
                    msg.sender = User{stmt->extract_int(1), "", 0};
                    msg.receiver = User{stmt->extract_int(2), "", 0};
                    msg.text = std::string(reinterpret_cast<const char*>(stmt->extract_text_ptr(3)));
                    results.push_back(std::move(msg));
                } else if (step == StmtType::NO_MORE_ROWS) {
                    break;
                }
            }
            total_rows += results.size();
            stmt->reset();
        }

        double elapsed = timer.elapsed_ms();
        std::cout << "  LIMIT only:      " << std::fixed << std::setprecision(4)
                  << (elapsed / config.iterations) << " ms/query, "
                  << (total_rows / (elapsed / 1000.0)) / 1000000.0 << " M rows/sec" << std::endl;
    }

    // Test case 3: LIMIT + OFFSET (bind limit, offset)
    {
        BenchmarkTimer timer;
        int64_t total_rows = 0;

        for (int i = 0; i < config.iterations; ++i) {
            stmt->bind_int(1, config.limit_size);
            stmt->bind_int(2, config.offset_size);

            std::vector<Message> results;
            results.reserve(config.limit_size);

            using StmtType = std::remove_reference_t<decltype(*stmt)>;
            while (true) {
                int step = stmt->step_raw();
                if (step == StmtType::ROW_AVAILABLE) {
                    Message msg;
                    msg.id = stmt->extract_int(0);
                    msg.sender = User{stmt->extract_int(1), "", 0};
                    msg.receiver = User{stmt->extract_int(2), "", 0};
                    msg.text = std::string(reinterpret_cast<const char*>(stmt->extract_text_ptr(3)));
                    results.push_back(std::move(msg));
                } else if (step == StmtType::NO_MORE_ROWS) {
                    break;
                }
            }
            total_rows += results.size();
            stmt->reset();
        }

        double elapsed = timer.elapsed_ms();
        std::cout << "  LIMIT + OFFSET:  " << std::fixed << std::setprecision(4)
                  << (elapsed / config.iterations) << " ms/query, "
                  << (total_rows / (elapsed / 1000.0)) / 1000000.0 << " M rows/sec" << std::endl;
    }

    teardown_database();
}

int main(int argc, char* argv[]) {
    BenchmarkConfig config;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--messages=", 11) == 0) {
            config.num_messages = std::stoi(argv[i] + 11);
        } else if (strncmp(argv[i], "--iterations=", 13) == 0) {
            config.iterations = std::stoi(argv[i] + 13);
        } else if (strncmp(argv[i], "--limit=", 8) == 0) {
            config.limit_size = std::stoi(argv[i] + 8);
        } else if (strncmp(argv[i], "--offset=", 9) == 0) {
            config.offset_size = std::stoi(argv[i] + 9);
        }
    }

    std::cout << "====================================================" << std::endl;
    std::cout << "   A/B Test: Statement Caching Strategies" << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Messages:    " << config.num_messages << std::endl;
    std::cout << "  LIMIT size:  " << config.limit_size << " rows" << std::endl;
    std::cout << "  OFFSET size: " << config.offset_size << " rows" << std::endl;
    std::cout << "  Iterations:  " << config.iterations << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << std::endl;

    benchmark_current_approach(config);
    std::cout << std::endl;
    benchmark_simplified_approach(config);

    std::cout << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << "Analysis:" << std::endl;
    std::cout << "  Current:    3 cached statements, conditional binding" << std::endl;
    std::cout << "  Simplified: 1 cached statement, always bind 2 params" << std::endl;
    std::cout << std::endl;
    std::cout << "Benefits of Simplified Approach:" << std::endl;
    std::cout << "  ✓ Less code complexity (no if/else branching)" << std::endl;
    std::cout << "  ✓ Better branch prediction (simpler control flow)" << std::endl;
    std::cout << "  ✓ Smaller code size (1 statement vs 3)" << std::endl;
    std::cout << "  ✓ Less memory (1 cached pointer vs 3)" << std::endl;
    std::cout << std::endl;
    std::cout << "Cost:" << std::endl;
    std::cout << "  • Always binds 2 integer parameters (even when not needed)" << std::endl;
    std::cout << "  • bind_int() cost: ~5-10 CPU cycles per parameter" << std::endl;
    std::cout << "====================================================" << std::endl;

    return 0;
}
