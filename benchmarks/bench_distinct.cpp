#include <cstring>
#include <iostream>
#include <iomanip>
#include "benchmark_utils.hpp"

import storm;
import <string>;
import <vector>;
import <span>;
import <expected>;
import <tuple>;
import <memory>;

using namespace storm;
using namespace storm::benchmark;

// Test model for DISTINCT benchmarks
struct Person {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
};

// Additional models for JOIN benchmarks
struct User {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
};

struct Message {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               content;
    [[= storm::meta::FieldAttr::fk]] User     sender;
};

// Setup database with test data containing duplicates
void setup_database(int num_records, int num_unique_names = 100, int num_unique_ages = 50) {
    auto result = QuerySet<Person>::set_default_connection(":memory:");
    if (!result.has_value()) {
        throw std::runtime_error("Failed to open database");
    }

    auto& conn = QuerySet<Person>::get_default_connection();

    // Create table
    auto create_result = conn.execute(
            "CREATE TABLE Person ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL, "
            "age INTEGER NOT NULL"
            ")"
    );
    if (!create_result.has_value()) {
        throw std::runtime_error("Failed to create Person table");
    }

    // Insert records with controlled duplicates
    QuerySet<Person>    person_qs;
    std::vector<Person> people;
    people.reserve(num_records);
    for (int i = 0; i < num_records; ++i) {
        int name_idx = i % num_unique_names;
        int age_idx  = i % num_unique_ages;
        people.push_back({0, "Person" + std::to_string(name_idx), 20 + age_idx});
    }
    auto insert_result = person_qs.insert(std::span<const Person>(people));
    if (!insert_result.has_value()) {
        throw std::runtime_error("Failed to insert people");
    }
}

void teardown_database() {
    QuerySet<Person>::clear_default_connection();
}

// Benchmark: Storm ORM DISTINCT on name field
void benchmark_storm_distinct_name(int num_records, int iterations = 100) {
    setup_database(num_records);

    QuerySet<Person> person_qs;
    BenchmarkTimer   timer;
    double           total_time    = 0;
    int              total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto   result  = person_qs.distinct<^^Person::name>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_results += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "Storm ORM DISTINCT (name) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (total_results / (total_time / 1000.0))
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Storm ORM DISTINCT on age field
void benchmark_storm_distinct_age(int num_records, int iterations = 100) {
    setup_database(num_records);

    QuerySet<Person> person_qs;
    BenchmarkTimer   timer;
    double           total_time    = 0;
    int              total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto   result  = person_qs.distinct<^^Person::age>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_results += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "Storm ORM DISTINCT (age) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (total_results / (total_time / 1000.0))
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Storm ORM DISTINCT on id field (primary key)
void benchmark_storm_distinct_id(int num_records, int iterations = 100) {
    setup_database(num_records);

    QuerySet<Person> person_qs;
    BenchmarkTimer   timer;
    double           total_time    = 0;
    int              total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto   result  = person_qs.distinct().select(); // Defaults to PK
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_results += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "Storm ORM DISTINCT (id/PK) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (total_results / (total_time / 1000.0))
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite DISTINCT on name field
void benchmark_raw_distinct_name(int num_records, int iterations = 100) {
    setup_database(num_records);

    auto&       conn = QuerySet<Person>::get_default_connection();
    std::string sql  = "SELECT DISTINCT name FROM Person";

    BenchmarkTimer timer;
    double         total_time    = 0;
    int            total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto                     stmt = std::move(stmt_result.value());
        std::vector<std::string> results;
        results.reserve(100);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                results.push_back(std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(0))));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_results += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite DISTINCT (name) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (total_results / (total_time / 1000.0))
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite DISTINCT on age field
void benchmark_raw_distinct_age(int num_records, int iterations = 100) {
    setup_database(num_records);

    auto&       conn = QuerySet<Person>::get_default_connection();
    std::string sql  = "SELECT DISTINCT age FROM Person";

    BenchmarkTimer timer;
    double         total_time    = 0;
    int            total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto             stmt = std::move(stmt_result.value());
        std::vector<int> results;
        results.reserve(50);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                results.push_back(stmt.extract_int(0));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_results += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite DISTINCT (age) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (total_results / (total_time / 1000.0))
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite DISTINCT on id field
void benchmark_raw_distinct_id(int num_records, int iterations = 100) {
    setup_database(num_records);

    auto&       conn = QuerySet<Person>::get_default_connection();
    std::string sql  = "SELECT DISTINCT id FROM Person";

    BenchmarkTimer timer;
    double         total_time    = 0;
    int            total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto             stmt = std::move(stmt_result.value());
        std::vector<int> results;
        results.reserve(num_records);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                results.push_back(stmt.extract_int(0));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_results += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite DISTINCT (id) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (total_results / (total_time / 1000.0))
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Storm ORM DISTINCT on two fields (name, age)
void benchmark_storm_distinct_name_age(int num_records, int iterations = 100) {
    setup_database(num_records);

    QuerySet<Person> person_qs;
    BenchmarkTimer   timer;
    double           total_time    = 0;
    int              total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto   result  = person_qs.distinct<^^Person::name, ^^Person::age>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_results += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "Storm ORM DISTINCT (name, age) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (total_results / (total_time / 1000.0))
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Storm ORM DISTINCT on three fields (id, name, age)
void benchmark_storm_distinct_id_name_age(int num_records, int iterations = 100) {
    setup_database(num_records);

    QuerySet<Person> person_qs;
    BenchmarkTimer   timer;
    double           total_time    = 0;
    int              total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto   result  = person_qs.distinct<^^Person::id, ^^Person::name, ^^Person::age>().select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_results += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "Storm ORM DISTINCT (id, name, age) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (total_results / (total_time / 1000.0))
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite DISTINCT on two fields (name, age)
void benchmark_raw_distinct_name_age(int num_records, int iterations = 100) {
    setup_database(num_records);

    auto&       conn = QuerySet<Person>::get_default_connection();
    std::string sql  = "SELECT DISTINCT name, age FROM Person";

    BenchmarkTimer timer;
    double         total_time    = 0;
    int            total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto                                      stmt = std::move(stmt_result.value());
        std::vector<std::tuple<std::string, int>> results;
        results.reserve(100);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                std::string name(reinterpret_cast<const char*>(stmt.extract_text_ptr(0)));
                int         age = stmt.extract_int(1);
                results.emplace_back(name, age);
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_results += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite DISTINCT (name, age) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (total_results / (total_time / 1000.0))
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite DISTINCT on three fields (id, name, age)
void benchmark_raw_distinct_id_name_age(int num_records, int iterations = 100) {
    setup_database(num_records);

    auto&       conn = QuerySet<Person>::get_default_connection();
    std::string sql  = "SELECT DISTINCT id, name, age FROM Person";

    BenchmarkTimer timer;
    double         total_time    = 0;
    int            total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto stmt_result = conn.prepare(sql);
        if (!stmt_result.has_value()) {
            std::cerr << "Failed to prepare statement" << std::endl;
            break;
        }

        auto                                           stmt = std::move(stmt_result.value());
        std::vector<std::tuple<int, std::string, int>> results;
        results.reserve(num_records);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                int         id = stmt.extract_int(0);
                std::string name(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                int         age = stmt.extract_int(2);
                results.emplace_back(id, name, age);
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_results += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite DISTINCT (id, name, age) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (total_results / (total_time / 1000.0))
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// ============================================================================
// NEW BENCHMARKS: DISTINCT with WHERE and JOIN
// ============================================================================

// Benchmark: Storm ORM DISTINCT with WHERE clause
void benchmark_storm_distinct_where(int num_records, int iterations = 100) {
    setup_database(num_records);

    QuerySet<Person> person_qs;
    BenchmarkTimer   timer;
    double           total_time    = 0;
    int              total_results = 0;

    // OPTIMIZATION: Create WHERE expression once outside loop to avoid repeated allocations
    auto where_expr = storm::orm::where::field<^^Person::age>() > 30;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        // SELECT DISTINCT name WHERE age > 30
        auto   result  = person_qs.where(where_expr)
                               .distinct<^^Person::name>()
                               .select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_results += result.value().size();
            total_time += elapsed;
        }

        // Reset WHERE state to avoid compounding ANDs
        person_qs.reset();
    }

    std::cout << "Storm ORM DISTINCT (name) + WHERE (age > 30) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (total_results / (total_time / 1000.0))
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Benchmark: Raw SQLite DISTINCT with WHERE clause
void benchmark_raw_distinct_where(int num_records, int iterations = 100) {
    setup_database(num_records);

    auto&       conn = QuerySet<Person>::get_default_connection();
    std::string sql  = "SELECT DISTINCT name FROM Person WHERE age > ?";

    BenchmarkTimer timer;
    double         total_time    = 0;
    int            total_results = 0;

    // OPTIMIZATION: Prepare statement ONCE outside loop (fair comparison with Storm's caching)
    auto stmt_result = conn.prepare(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_database();
        return;
    }
    auto stmt = std::move(stmt_result.value());

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        // Reset and bind parameter (reuse prepared statement)
        stmt.reset();
        stmt.bind_int(1, 30);

        std::vector<std::string> results;
        results.reserve(100);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                results.emplace_back(reinterpret_cast<const char*>(stmt.extract_text_ptr(0)));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_results += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite DISTINCT (name) + WHERE (age > 30) - " << num_records << " records:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (total_results / (total_time / 1000.0))
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Setup database for JOIN benchmarks
void setup_join_database(int num_messages) {
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

    // Create Message table
    auto create_msg = conn.execute(
            "CREATE TABLE Message ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "content TEXT NOT NULL, "
            "sender_id INTEGER NOT NULL, "
            "FOREIGN KEY (sender_id) REFERENCES User(id)"
            ")"
    );
    if (!create_msg.has_value()) {
        throw std::runtime_error("Failed to create Message table");
    }

    // Insert test users (fewer unique users than messages)
    int num_users = std::min(100, num_messages / 10);
    for (int i = 0; i < num_users; ++i) {
        std::string sql = "INSERT INTO User (name, age) VALUES ('User" + std::to_string(i) + "', " + std::to_string(20 + (i % 50)) + ")";
        auto user_insert = conn.execute(sql);
        if (!user_insert.has_value()) {
            throw std::runtime_error("Failed to insert user");
        }
    }

    // Insert messages
    for (int i = 0; i < num_messages; ++i) {
        int sender_id = 1 + (i % num_users);
        std::string sql = "INSERT INTO Message (content, sender_id) VALUES ('Message" + std::to_string(i) + "', " + std::to_string(sender_id) + ")";
        auto msg_insert = conn.execute(sql);
        if (!msg_insert.has_value()) {
            throw std::runtime_error("Failed to insert message");
        }
    }
}

void teardown_join_database() {
    QuerySet<User>::clear_default_connection();
}

// Benchmark: Storm ORM DISTINCT with JOIN
void benchmark_storm_distinct_join(int num_messages, int iterations = 100) {
    setup_join_database(num_messages);

    QuerySet<Message> msg_qs;
    BenchmarkTimer    timer;
    double            total_time    = 0;
    int               total_results = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        // SELECT DISTINCT content FROM Message JOIN User
        auto   result  = msg_qs.join<&Message::sender>()
                               .distinct<^^Message::content>()
                               .select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_results += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "Storm ORM DISTINCT (content) + JOIN - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (total_results / (total_time / 1000.0))
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_join_database();
}

// Benchmark: Raw SQLite DISTINCT with JOIN
void benchmark_raw_distinct_join(int num_messages, int iterations = 100) {
    setup_join_database(num_messages);

    auto&       conn = QuerySet<User>::get_default_connection();
    std::string sql  = "SELECT DISTINCT content FROM Message m INNER JOIN User u ON m.sender_id = u.id";

    BenchmarkTimer timer;
    double         total_time    = 0;
    int            total_results = 0;

    // OPTIMIZATION: Prepare statement ONCE outside loop (fair comparison with Storm's caching)
    auto stmt_result = conn.prepare(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_join_database();
        return;
    }
    auto stmt = std::move(stmt_result.value());

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        // Reset statement for reuse
        stmt.reset();

        std::vector<std::string> results;
        results.reserve(num_messages);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                results.emplace_back(reinterpret_cast<const char*>(stmt.extract_text_ptr(0)));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_results += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite DISTINCT (content) + JOIN - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (total_results / (total_time / 1000.0))
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_join_database();
}

// Benchmark: Storm ORM DISTINCT with WHERE + JOIN
void benchmark_storm_distinct_where_join(int num_messages, int iterations = 100) {
    setup_join_database(num_messages);

    QuerySet<Message> msg_qs;
    BenchmarkTimer    timer;
    double            total_time    = 0;
    int               total_results = 0;

    // OPTIMIZATION: Create WHERE expression once outside loop
    auto where_expr = storm::orm::where::field<^^Message::content>().like("%0%");

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        // SELECT DISTINCT content FROM Message JOIN User WHERE content LIKE '%0%'
        auto   result  = msg_qs.join<&Message::sender>()
                               .where(where_expr)
                               .distinct<^^Message::content>()
                               .select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_results += result.value().size();
            total_time += elapsed;
        }

        // Reset WHERE/JOIN state to avoid compounding
        msg_qs.reset();
    }

    std::cout << "Storm ORM DISTINCT (content) + WHERE + JOIN - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (total_results / (total_time / 1000.0))
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_join_database();
}

// Benchmark: Raw SQLite DISTINCT with WHERE + JOIN
void benchmark_raw_distinct_where_join(int num_messages, int iterations = 100) {
    setup_join_database(num_messages);

    auto&       conn = QuerySet<User>::get_default_connection();
    std::string sql  = "SELECT DISTINCT content FROM Message m INNER JOIN User u ON m.sender_id = u.id WHERE content LIKE ?";

    BenchmarkTimer timer;
    double         total_time    = 0;
    int            total_results = 0;

    // OPTIMIZATION: Prepare statement ONCE outside loop
    auto stmt_result = conn.prepare(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_join_database();
        return;
    }
    auto stmt = std::move(stmt_result.value());

    for (int i = 0; i < iterations; ++i) {
        timer.reset();

        // Reset and bind parameter
        stmt.reset();
        stmt.bind_text(1, "%0%");

        std::vector<std::string> results;
        results.reserve(num_messages / 10);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                results.emplace_back(reinterpret_cast<const char*>(stmt.extract_text_ptr(0)));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        total_results += results.size();
        total_time += elapsed;
    }

    std::cout << "Raw SQLite DISTINCT (content) + WHERE + JOIN - " << num_messages << " messages:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(0) << (total_results / (total_time / 1000.0))
              << " rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_join_database();
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --size=N          Number of records (default: 10000)" << std::endl;
    std::cout << "  --iterations=N    Number of iterations (default: 100)" << std::endl;
    std::cout << std::endl;
    std::cout << "Benchmark Selection (run all if none specified):" << std::endl;
    std::cout << "Single Field DISTINCT:" << std::endl;
    std::cout << "  --storm-name         Run Storm ORM DISTINCT on name field" << std::endl;
    std::cout << "  --storm-age          Run Storm ORM DISTINCT on age field" << std::endl;
    std::cout << "  --storm-id           Run Storm ORM DISTINCT on id field (PK)" << std::endl;
    std::cout << "  --raw-name           Run raw SQL DISTINCT on name field" << std::endl;
    std::cout << "  --raw-age            Run raw SQL DISTINCT on age field" << std::endl;
    std::cout << "  --raw-id             Run raw SQL DISTINCT on id field" << std::endl;
    std::cout << std::endl;
    std::cout << "Multi-Field DISTINCT:" << std::endl;
    std::cout << "  --storm-name-age     Run Storm ORM DISTINCT on (name, age)" << std::endl;
    std::cout << "  --storm-id-name-age  Run Storm ORM DISTINCT on (id, name, age)" << std::endl;
    std::cout << "  --raw-name-age       Run raw SQL DISTINCT on (name, age)" << std::endl;
    std::cout << "  --raw-id-name-age    Run raw SQL DISTINCT on (id, name, age)" << std::endl;
    std::cout << std::endl;
    std::cout << "DISTINCT with WHERE/JOIN:" << std::endl;
    std::cout << "  --storm-where        Run Storm ORM DISTINCT + WHERE" << std::endl;
    std::cout << "  --raw-where          Run raw SQL DISTINCT + WHERE" << std::endl;
    std::cout << "  --storm-join         Run Storm ORM DISTINCT + JOIN" << std::endl;
    std::cout << "  --raw-join           Run raw SQL DISTINCT + JOIN" << std::endl;
    std::cout << "  --storm-where-join   Run Storm ORM DISTINCT + WHERE + JOIN" << std::endl;
    std::cout << "  --raw-where-join     Run raw SQL DISTINCT + WHERE + JOIN" << std::endl;
    std::cout << std::endl;
    std::cout << "  --help, -h           Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    int test_size  = 10000;
    int iterations = 100;

    // Benchmark selection flags - single field
    bool run_storm_name = false;
    bool run_storm_age  = false;
    bool run_storm_id   = false;
    bool run_raw_name   = false;
    bool run_raw_age    = false;
    bool run_raw_id     = false;

    // Benchmark selection flags - multi-field
    bool run_storm_name_age    = false;
    bool run_storm_id_name_age = false;
    bool run_raw_name_age      = false;
    bool run_raw_id_name_age   = false;

    // Benchmark selection flags - WHERE/JOIN
    bool run_storm_where      = false;
    bool run_raw_where        = false;
    bool run_storm_join       = false;
    bool run_raw_join         = false;
    bool run_storm_where_join = false;
    bool run_raw_where_join   = false;

    bool run_all = true; // Default: run everything

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--size=", 7) == 0) {
            test_size = std::stoi(argv[i] + 7);
        } else if (strncmp(argv[i], "--iterations=", 13) == 0) {
            iterations = std::stoi(argv[i] + 13);
        } else if (strcmp(argv[i], "--storm-name") == 0) {
            run_storm_name = true;
            run_all        = false;
        } else if (strcmp(argv[i], "--storm-age") == 0) {
            run_storm_age = true;
            run_all       = false;
        } else if (strcmp(argv[i], "--storm-id") == 0) {
            run_storm_id = true;
            run_all      = false;
        } else if (strcmp(argv[i], "--raw-name") == 0) {
            run_raw_name = true;
            run_all      = false;
        } else if (strcmp(argv[i], "--raw-age") == 0) {
            run_raw_age = true;
            run_all     = false;
        } else if (strcmp(argv[i], "--raw-id") == 0) {
            run_raw_id = true;
            run_all    = false;
        } else if (strcmp(argv[i], "--storm-name-age") == 0) {
            run_storm_name_age = true;
            run_all            = false;
        } else if (strcmp(argv[i], "--storm-id-name-age") == 0) {
            run_storm_id_name_age = true;
            run_all               = false;
        } else if (strcmp(argv[i], "--raw-name-age") == 0) {
            run_raw_name_age = true;
            run_all          = false;
        } else if (strcmp(argv[i], "--raw-id-name-age") == 0) {
            run_raw_id_name_age = true;
            run_all             = false;
        } else if (strcmp(argv[i], "--storm-where") == 0) {
            run_storm_where = true;
            run_all         = false;
        } else if (strcmp(argv[i], "--raw-where") == 0) {
            run_raw_where = true;
            run_all       = false;
        } else if (strcmp(argv[i], "--storm-join") == 0) {
            run_storm_join = true;
            run_all        = false;
        } else if (strcmp(argv[i], "--raw-join") == 0) {
            run_raw_join = true;
            run_all      = false;
        } else if (strcmp(argv[i], "--storm-where-join") == 0) {
            run_storm_where_join = true;
            run_all              = false;
        } else if (strcmp(argv[i], "--raw-where-join") == 0) {
            run_raw_where_join = true;
            run_all            = false;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    std::cout << "====================================================" << std::endl;
    std::cout << "    Storm ORM DISTINCT Performance Benchmark" << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << std::endl;

    std::cout << "Testing with " << test_size << " records (iterations: " << iterations << ")" << std::endl;
    std::cout << std::string(60, '-') << std::endl;
    std::cout << std::endl;

    // Run Storm ORM single-field benchmarks
    if (run_all || run_storm_name || run_storm_age || run_storm_id) {
        std::cout << "--- Storm ORM Single-Field DISTINCT Operations ---" << std::endl;
        if (run_all || run_storm_name) {
            benchmark_storm_distinct_name(test_size, iterations);
        }
        if (run_all || run_storm_age) {
            benchmark_storm_distinct_age(test_size, iterations);
        }
        if (run_all || run_storm_id) {
            benchmark_storm_distinct_id(test_size, iterations);
        }
        std::cout << std::endl;
    }

    // Run Raw SQLite single-field benchmarks
    if (run_all || run_raw_name || run_raw_age || run_raw_id) {
        std::cout << "--- Raw SQLite Single-Field DISTINCT Operations ---" << std::endl;
        if (run_all || run_raw_name) {
            benchmark_raw_distinct_name(test_size, iterations);
        }
        if (run_all || run_raw_age) {
            benchmark_raw_distinct_age(test_size, iterations);
        }
        if (run_all || run_raw_id) {
            benchmark_raw_distinct_id(test_size, iterations);
        }
        std::cout << std::endl;
    }

    // Run Storm ORM multi-field benchmarks
    if (run_all || run_storm_name_age || run_storm_id_name_age) {
        std::cout << "--- Storm ORM Multi-Field DISTINCT Operations ---" << std::endl;
        if (run_all || run_storm_name_age) {
            benchmark_storm_distinct_name_age(test_size, iterations);
        }
        if (run_all || run_storm_id_name_age) {
            benchmark_storm_distinct_id_name_age(test_size, iterations);
        }
        std::cout << std::endl;
    }

    // Run Raw SQLite multi-field benchmarks
    if (run_all || run_raw_name_age || run_raw_id_name_age) {
        std::cout << "--- Raw SQLite Multi-Field DISTINCT Operations ---" << std::endl;
        if (run_all || run_raw_name_age) {
            benchmark_raw_distinct_name_age(test_size, iterations);
        }
        if (run_all || run_raw_id_name_age) {
            benchmark_raw_distinct_id_name_age(test_size, iterations);
        }
        std::cout << std::endl;
    }

    // Run DISTINCT with WHERE benchmarks
    if (run_all || run_storm_where || run_raw_where) {
        std::cout << "--- DISTINCT with WHERE Clause ---" << std::endl;
        if (run_all || run_storm_where) {
            benchmark_storm_distinct_where(test_size, iterations);
        }
        if (run_all || run_raw_where) {
            benchmark_raw_distinct_where(test_size, iterations);
        }
        std::cout << std::endl;
    }

    // Run DISTINCT with JOIN benchmarks
    if (run_all || run_storm_join || run_raw_join) {
        std::cout << "--- DISTINCT with JOIN ---" << std::endl;
        if (run_all || run_storm_join) {
            benchmark_storm_distinct_join(test_size, iterations);
        }
        if (run_all || run_raw_join) {
            benchmark_raw_distinct_join(test_size, iterations);
        }
        std::cout << std::endl;
    }

    // Run DISTINCT with WHERE + JOIN benchmarks
    if (run_all || run_storm_where_join || run_raw_where_join) {
        std::cout << "--- DISTINCT with WHERE + JOIN ---" << std::endl;
        if (run_all || run_storm_where_join) {
            benchmark_storm_distinct_where_join(test_size, iterations);
        }
        if (run_all || run_raw_where_join) {
            benchmark_raw_distinct_where_join(test_size, iterations);
        }
        std::cout << std::endl;
    }

    std::cout << std::string(60, '=') << std::endl;
    std::cout << std::endl;

    return 0;
}
