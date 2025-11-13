#include <cstring>
#include <iostream>
#include <iomanip>
#include "benchmark_utils.hpp"

import storm;
import <string>;
import <vector>;
import <span>;
import <expected>;
import <variant>;
import <memory>;

using namespace storm;
using namespace storm::benchmark;
using namespace storm::orm::where;

// Extended test model for comprehensive WHERE benchmarks
struct TestPerson {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string                               name;
    int                                       age;
    double                                    salary;
    bool                                      is_active;
};

// Setup database with test data
void setup_database(int num_people) {
    auto result = QuerySet<TestPerson>::set_default_connection(":memory:");
    if (!result.has_value()) {
        throw std::runtime_error("Failed to open database");
    }

    auto& conn = QuerySet<TestPerson>::get_default_connection();

    // Create table
    auto create_result = conn.execute(
            "CREATE TABLE TestPerson ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL, "
            "age INTEGER NOT NULL, "
            "salary REAL NOT NULL, "
            "is_active INTEGER NOT NULL"
            ")"
    );
    if (!create_result.has_value()) {
        throw std::runtime_error("Failed to create table");
    }

    // Insert people with varied data
    QuerySet<TestPerson>    person_qs;
    std::vector<TestPerson> people;
    people.reserve(num_people);
    for (int i = 0; i < num_people; ++i) {
        people.push_back({
                0,                            // id (auto-generated)
                "Person" + std::to_string(i), // name
                20 + (i % 50),                // age (20-69)
                30000.0 + (i % 70000),        // salary (30k-100k)
                (i % 3) != 0                  // is_active (2/3 true, 1/3 false)
        });
    }

    auto insert_result = person_qs.insert(std::span<const TestPerson>(people));
    if (!insert_result.has_value()) {
        throw std::runtime_error("Failed to insert people");
    }
}

void teardown_database() {
    QuerySet<TestPerson>::clear_default_connection();
}

//==============================================================================
// BASELINE BENCHMARKS
//==============================================================================

void benchmark_select_no_where(int num_people, int iterations = 100) {
    setup_database(num_people);

    QuerySet<TestPerson> person_qs;
    BenchmarkTimer       timer;
    double               total_time = 0;
    int                  total_rows = 0;

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto   result  = person_qs.select();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            total_rows += result.value().size();
            total_time += elapsed;
        }
    }

    std::cout << "SELECT (no WHERE) - " << num_people << " rows:" << std::endl;
    std::cout << "  Total time: " << std::fixed << std::setprecision(2) << total_time << " ms" << std::endl;
    std::cout << "  Iterations: " << iterations << std::endl;
    std::cout << "  Avg per iteration: " << std::fixed << std::setprecision(4) << (total_time / iterations) << " ms"
              << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << (total_rows / (total_time / 1000.0) / 1000000.0) << "M rows/sec" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

//==============================================================================
// DATA TYPE BENCHMARKS
//==============================================================================

// Integer comparison
void benchmark_where_int(int num_people, int iterations = 100) {
    setup_database(num_people);

    QuerySet<TestPerson> person_qs;
    BenchmarkTimer       timer;
    double               storm_time = 0;
    int                  storm_rows = 0;

    // Storm ORM
    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = person_qs.where(field<^^TestPerson::age>() > 30).select();
        person_qs.reset();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            storm_rows += result.value().size();
            storm_time += elapsed;
        }
    }

    // Raw SQLite
    auto&       conn = QuerySet<TestPerson>::get_default_connection();
    std::string sql  = "SELECT id, name, age, salary, is_active FROM TestPerson WHERE age > ?";

    timer.reset();
    double raw_time = 0;
    int    raw_rows = 0;

    // Prepare statement once outside loop
    auto stmt_result = conn.prepare(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_database();
        return;
    }
    auto stmt = std::move(stmt_result.value());

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        stmt.reset(); // Reset statement, don't re-prepare
        stmt.bind_int(1, 30);

        std::vector<TestPerson> results;
        results.reserve(num_people);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                TestPerson p;
                p.id        = stmt.extract_int(0);
                p.name      = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                p.age       = stmt.extract_int(2);
                p.salary    = stmt.extract_double(3);
                p.is_active = stmt.extract_int(4) != 0;
                results.push_back(std::move(p));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        raw_rows += results.size();
        raw_time += elapsed;
    }

    double storm_throughput = storm_rows / (storm_time / 1000.0) / 1000000.0;
    double raw_throughput   = raw_rows / (raw_time / 1000.0) / 1000000.0;
    double efficiency       = (storm_throughput / raw_throughput) * 100.0;

    std::cout << "WHERE (int: age > 30):" << std::endl;
    std::cout << "  Storm ORM: " << std::fixed << std::setprecision(2) << storm_throughput << "M rows/sec" << std::endl;
    std::cout << "  Raw SQLite: " << std::fixed << std::setprecision(2) << raw_throughput << "M rows/sec" << std::endl;
    std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << efficiency << "%" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// String equality comparison
void benchmark_where_string(int num_people, int iterations = 100) {
    setup_database(num_people);

    QuerySet<TestPerson> person_qs;
    BenchmarkTimer       timer;
    double               storm_time = 0;
    int                  storm_rows = 0;

    // Storm ORM - use LIKE to match more rows (Person5% matches Person5, Person50, Person500, etc.)
    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = person_qs.where(field<^^TestPerson::name>().like("Person5%")).select();
        person_qs.reset();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            storm_rows += result.value().size();
            storm_time += elapsed;
        }
    }

    // Raw SQLite
    auto&       conn = QuerySet<TestPerson>::get_default_connection();
    std::string sql  = "SELECT id, name, age, salary, is_active FROM TestPerson WHERE name LIKE ?";

    timer.reset();
    double raw_time = 0;
    int    raw_rows = 0;

    // Prepare statement once outside loop
    auto stmt_result = conn.prepare(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_database();
        return;
    }
    auto stmt = std::move(stmt_result.value());

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        stmt.reset(); // Reset statement, don't re-prepare
        stmt.bind_text(1, "Person5%");

        std::vector<TestPerson> results;
        results.reserve(num_people);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                TestPerson p;
                p.id        = stmt.extract_int(0);
                p.name      = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                p.age       = stmt.extract_int(2);
                p.salary    = stmt.extract_double(3);
                p.is_active = stmt.extract_int(4) != 0;
                results.push_back(std::move(p));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        raw_rows += results.size();
        raw_time += elapsed;
    }

    double storm_throughput = storm_rows / (storm_time / 1000.0) / 1000000.0;
    double raw_throughput   = raw_rows / (raw_time / 1000.0) / 1000000.0;
    double efficiency       = (storm_throughput / raw_throughput) * 100.0;

    std::cout << "WHERE (string: name LIKE \"Person5%\"):" << std::endl;
    std::cout << "  Storm ORM: " << std::fixed << std::setprecision(2) << storm_throughput << "M rows/sec" << std::endl;
    std::cout << "  Raw SQLite: " << std::fixed << std::setprecision(2) << raw_throughput << "M rows/sec" << std::endl;
    std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << efficiency << "%" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Float comparison
void benchmark_where_float(int num_people, int iterations = 100) {
    setup_database(num_people);

    QuerySet<TestPerson> person_qs;
    BenchmarkTimer       timer;
    double               storm_time = 0;
    int                  storm_rows = 0;

    // Storm ORM - salary ranges from 30k to ~40k, so use 35k to match ~50% of rows
    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = person_qs.where(field<^^TestPerson::salary>() > 35000.0).select();
        person_qs.reset();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            storm_rows += result.value().size();
            storm_time += elapsed;
        }
    }

    // Raw SQLite
    auto&       conn = QuerySet<TestPerson>::get_default_connection();
    std::string sql  = "SELECT id, name, age, salary, is_active FROM TestPerson WHERE salary > ?";

    timer.reset();
    double raw_time = 0;
    int    raw_rows = 0;

    // Prepare statement once outside loop
    auto stmt_result = conn.prepare(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_database();
        return;
    }
    auto stmt = std::move(stmt_result.value());

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        stmt.reset(); // Reset statement, don't re-prepare
        stmt.bind_double(1, 35000.0);

        std::vector<TestPerson> results;
        results.reserve(num_people);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                TestPerson p;
                p.id        = stmt.extract_int(0);
                p.name      = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                p.age       = stmt.extract_int(2);
                p.salary    = stmt.extract_double(3);
                p.is_active = stmt.extract_int(4) != 0;
                results.push_back(std::move(p));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        raw_rows += results.size();
        raw_time += elapsed;
    }

    double storm_throughput = storm_rows / (storm_time / 1000.0) / 1000000.0;
    double raw_throughput   = raw_rows / (raw_time / 1000.0) / 1000000.0;
    double efficiency       = (storm_throughput / raw_throughput) * 100.0;

    std::cout << "WHERE (float: salary > 35000.0):" << std::endl;
    std::cout << "  Storm ORM: " << std::fixed << std::setprecision(2) << storm_throughput << "M rows/sec" << std::endl;
    std::cout << "  Raw SQLite: " << std::fixed << std::setprecision(2) << raw_throughput << "M rows/sec" << std::endl;
    std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << efficiency << "%" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Bool comparison
void benchmark_where_bool(int num_people, int iterations = 100) {
    setup_database(num_people);

    QuerySet<TestPerson> person_qs;
    BenchmarkTimer       timer;
    double               storm_time = 0;
    int                  storm_rows = 0;

    // Storm ORM
    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = person_qs.where(field<^^TestPerson::is_active>() == true).select();
        person_qs.reset();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            storm_rows += result.value().size();
            storm_time += elapsed;
        }
    }

    // Raw SQLite
    auto&       conn = QuerySet<TestPerson>::get_default_connection();
    std::string sql  = "SELECT id, name, age, salary, is_active FROM TestPerson WHERE is_active = ?";

    timer.reset();
    double raw_time = 0;
    int    raw_rows = 0;

    // Prepare statement once outside loop
    auto stmt_result = conn.prepare(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_database();
        return;
    }
    auto stmt = std::move(stmt_result.value());

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        stmt.reset();        // Reset statement, don't re-prepare
        stmt.bind_int(1, 1); // true = 1

        std::vector<TestPerson> results;
        results.reserve(num_people);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                TestPerson p;
                p.id        = stmt.extract_int(0);
                p.name      = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                p.age       = stmt.extract_int(2);
                p.salary    = stmt.extract_double(3);
                p.is_active = stmt.extract_int(4) != 0;
                results.push_back(std::move(p));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        raw_rows += results.size();
        raw_time += elapsed;
    }

    double storm_throughput = storm_rows / (storm_time / 1000.0) / 1000000.0;
    double raw_throughput   = raw_rows / (raw_time / 1000.0) / 1000000.0;
    double efficiency       = (storm_throughput / raw_throughput) * 100.0;

    std::cout << "WHERE (bool: is_active == true):" << std::endl;
    std::cout << "  Storm ORM: " << std::fixed << std::setprecision(2) << storm_throughput << "M rows/sec" << std::endl;
    std::cout << "  Raw SQLite: " << std::fixed << std::setprecision(2) << raw_throughput << "M rows/sec" << std::endl;
    std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << efficiency << "%" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

//==============================================================================
// SPECIAL METHOD BENCHMARKS
//==============================================================================

// LIKE operator
void benchmark_where_like(int num_people, int iterations = 100) {
    setup_database(num_people);

    QuerySet<TestPerson> person_qs;
    BenchmarkTimer       timer;
    double               storm_time = 0;
    int                  storm_rows = 0;

    // Storm ORM
    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = person_qs.where(field<^^TestPerson::name>().like("Person5%")).select();
        person_qs.reset();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            storm_rows += result.value().size();
            storm_time += elapsed;
        }
    }

    // Raw SQLite
    auto&       conn = QuerySet<TestPerson>::get_default_connection();
    std::string sql  = "SELECT id, name, age, salary, is_active FROM TestPerson WHERE name LIKE ?";

    timer.reset();
    double raw_time = 0;
    int    raw_rows = 0;

    // Prepare statement once outside loop
    auto stmt_result = conn.prepare(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_database();
        return;
    }
    auto stmt = std::move(stmt_result.value());

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        stmt.reset(); // Reset statement, don't re-prepare
        stmt.bind_text(1, "Person5%");

        std::vector<TestPerson> results;
        results.reserve(num_people);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                TestPerson p;
                p.id        = stmt.extract_int(0);
                p.name      = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                p.age       = stmt.extract_int(2);
                p.salary    = stmt.extract_double(3);
                p.is_active = stmt.extract_int(4) != 0;
                results.push_back(std::move(p));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        raw_rows += results.size();
        raw_time += elapsed;
    }

    double storm_throughput = storm_rows / (storm_time / 1000.0) / 1000000.0;
    double raw_throughput   = raw_rows / (raw_time / 1000.0) / 1000000.0;
    double efficiency       = (storm_throughput / raw_throughput) * 100.0;

    std::cout << "WHERE (LIKE: name LIKE \"Person5%\"):" << std::endl;
    std::cout << "  Storm ORM: " << std::fixed << std::setprecision(2) << storm_throughput << "M rows/sec" << std::endl;
    std::cout << "  Raw SQLite: " << std::fixed << std::setprecision(2) << raw_throughput << "M rows/sec" << std::endl;
    std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << efficiency << "%" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// BETWEEN operator
void benchmark_where_between(int num_people, int iterations = 100) {
    setup_database(num_people);

    QuerySet<TestPerson> person_qs;
    BenchmarkTimer       timer;
    double               storm_time = 0;
    int                  storm_rows = 0;

    // Storm ORM
    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = person_qs.where(field<^^TestPerson::age>().between(28, 35)).select();
        person_qs.reset();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            storm_rows += result.value().size();
            storm_time += elapsed;
        }
    }

    // Raw SQLite
    auto&       conn = QuerySet<TestPerson>::get_default_connection();
    std::string sql  = "SELECT id, name, age, salary, is_active FROM TestPerson WHERE age BETWEEN ? AND ?";

    timer.reset();
    double raw_time = 0;
    int    raw_rows = 0;

    // Prepare statement once outside loop
    auto stmt_result = conn.prepare(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_database();
        return;
    }
    auto stmt = std::move(stmt_result.value());

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        stmt.reset(); // Reset statement, don't re-prepare
        stmt.bind_int(1, 28);
        stmt.bind_int(2, 35);

        std::vector<TestPerson> results;
        results.reserve(num_people);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                TestPerson p;
                p.id        = stmt.extract_int(0);
                p.name      = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                p.age       = stmt.extract_int(2);
                p.salary    = stmt.extract_double(3);
                p.is_active = stmt.extract_int(4) != 0;
                results.push_back(std::move(p));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        raw_rows += results.size();
        raw_time += elapsed;
    }

    double storm_throughput = storm_rows / (storm_time / 1000.0) / 1000000.0;
    double raw_throughput   = raw_rows / (raw_time / 1000.0) / 1000000.0;
    double efficiency       = (storm_throughput / raw_throughput) * 100.0;

    std::cout << "WHERE (BETWEEN: age BETWEEN 28 AND 35):" << std::endl;
    std::cout << "  Storm ORM: " << std::fixed << std::setprecision(2) << storm_throughput << "M rows/sec" << std::endl;
    std::cout << "  Raw SQLite: " << std::fixed << std::setprecision(2) << raw_throughput << "M rows/sec" << std::endl;
    std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << efficiency << "%" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// IN (3 values)
void benchmark_where_in_3_ct(int num_people, int iterations = 100) {
    setup_database(num_people);

    QuerySet<TestPerson> person_qs;
    BenchmarkTimer       timer;
    double               storm_time = 0;
    int                  storm_rows = 0;

    // Storm ORM
    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = person_qs.where(field<^^TestPerson::id>().in(100, 200, 300)).select();
        person_qs.reset();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            storm_rows += result.value().size();
            storm_time += elapsed;
        }
    }

    // Raw SQLite (same as before for comparison)
    auto&       conn = QuerySet<TestPerson>::get_default_connection();
    std::string sql  = "SELECT id, name, age, salary, is_active FROM TestPerson WHERE id IN (?, ?, ?)";

    timer.reset();
    double raw_time = 0;
    int    raw_rows = 0;

    // Prepare statement once outside loop
    auto stmt_result = conn.prepare(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_database();
        return;
    }
    auto stmt = std::move(stmt_result.value());

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        stmt.reset(); // Reset statement, don't re-prepare
        stmt.bind_int(1, 100);
        stmt.bind_int(2, 200);
        stmt.bind_int(3, 300);

        std::vector<TestPerson> results;
        results.reserve(num_people);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                TestPerson p;
                p.id        = stmt.extract_int(0);
                p.name      = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                p.age       = stmt.extract_int(2);
                p.salary    = stmt.extract_double(3);
                p.is_active = stmt.extract_int(4) != 0;
                results.push_back(std::move(p));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        raw_rows += results.size();
        raw_time += elapsed;
    }

    double storm_throughput = storm_rows / (storm_time / 1000.0) / 1000000.0;
    double raw_throughput   = raw_rows / (raw_time / 1000.0) / 1000000.0;
    double efficiency       = (storm_throughput / raw_throughput) * 100.0;

    std::cout << "WHERE (IN 3 values: id IN (100, 200, 300)):" << std::endl;
    std::cout << "  Storm ORM: " << std::fixed << std::setprecision(2) << storm_throughput << "M rows/sec" << std::endl;
    std::cout << "  Raw SQLite: " << std::fixed << std::setprecision(2) << raw_throughput << "M rows/sec" << std::endl;
    std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << efficiency << "%" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// IN (10 values)
void benchmark_where_in_10_ct(int num_people, int iterations = 100) {
    setup_database(num_people);

    QuerySet<TestPerson> person_qs;
    BenchmarkTimer       timer;
    double               storm_time = 0;
    int                  storm_rows = 0;

    // Storm ORM
    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = person_qs.where(field<^^TestPerson::id>().in(100, 200, 300, 400, 500, 600, 700, 800, 900, 1000))
                              .select();
        person_qs.reset();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            storm_rows += result.value().size();
            storm_time += elapsed;
        }
    }

    // Raw SQLite (same as before for comparison)
    auto&       conn = QuerySet<TestPerson>::get_default_connection();
    std::string sql =
            "SELECT id, name, age, salary, is_active FROM TestPerson WHERE id IN (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    timer.reset();
    double raw_time = 0;
    int    raw_rows = 0;

    // Prepare statement once outside loop
    auto stmt_result = conn.prepare(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_database();
        return;
    }
    auto stmt = std::move(stmt_result.value());

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        stmt.reset(); // Reset statement, don't re-prepare
        stmt.bind_int(1, 100);
        stmt.bind_int(2, 200);
        stmt.bind_int(3, 300);
        stmt.bind_int(4, 400);
        stmt.bind_int(5, 500);
        stmt.bind_int(6, 600);
        stmt.bind_int(7, 700);
        stmt.bind_int(8, 800);
        stmt.bind_int(9, 900);
        stmt.bind_int(10, 1000);

        std::vector<TestPerson> results;
        results.reserve(num_people);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                TestPerson p;
                p.id        = stmt.extract_int(0);
                p.name      = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                p.age       = stmt.extract_int(2);
                p.salary    = stmt.extract_double(3);
                p.is_active = stmt.extract_int(4) != 0;
                results.push_back(std::move(p));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        raw_rows += results.size();
        raw_time += elapsed;
    }

    double storm_throughput = storm_rows / (storm_time / 1000.0) / 1000000.0;
    double raw_throughput   = raw_rows / (raw_time / 1000.0) / 1000000.0;
    double efficiency       = (storm_throughput / raw_throughput) * 100.0;

    std::cout << "WHERE (IN 10 values: id IN (100...1000)):" << std::endl;
    std::cout << "  Storm ORM: " << std::fixed << std::setprecision(2) << storm_throughput << "M rows/sec" << std::endl;
    std::cout << "  Raw SQLite: " << std::fixed << std::setprecision(2) << raw_throughput << "M rows/sec" << std::endl;
    std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << efficiency << "%" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

//==============================================================================
// COMPLEX QUERY BENCHMARKS
//==============================================================================

// Simple: 2 conditions with AND
void benchmark_where_simple(int num_people, int iterations = 100) {
    setup_database(num_people);

    QuerySet<TestPerson> person_qs;
    BenchmarkTimer       timer;
    double               storm_time = 0;
    int                  storm_rows = 0;

    // Storm ORM
    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = person_qs.where(field<^^TestPerson::age>() > 25 and field<^^TestPerson::age>() < 50).select();
        person_qs.reset();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            storm_rows += result.value().size();
            storm_time += elapsed;
        }
    }

    // Raw SQLite
    auto&       conn = QuerySet<TestPerson>::get_default_connection();
    std::string sql  = "SELECT id, name, age, salary, is_active FROM TestPerson WHERE age > ? AND age < ?";

    timer.reset();
    double raw_time = 0;
    int    raw_rows = 0;

    // Prepare statement once outside loop
    auto stmt_result = conn.prepare(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_database();
        return;
    }
    auto stmt = std::move(stmt_result.value());

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        stmt.reset(); // Reset statement, don't re-prepare
        stmt.bind_int(1, 25);
        stmt.bind_int(2, 50);

        std::vector<TestPerson> results;
        results.reserve(num_people);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                TestPerson p;
                p.id        = stmt.extract_int(0);
                p.name      = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                p.age       = stmt.extract_int(2);
                p.salary    = stmt.extract_double(3);
                p.is_active = stmt.extract_int(4) != 0;
                results.push_back(std::move(p));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        raw_rows += results.size();
        raw_time += elapsed;
    }

    double storm_throughput = storm_rows / (storm_time / 1000.0) / 1000000.0;
    double raw_throughput   = raw_rows / (raw_time / 1000.0) / 1000000.0;
    double efficiency       = (storm_throughput / raw_throughput) * 100.0;

    std::cout << "WHERE (simple: age > 25 AND age < 50):" << std::endl;
    std::cout << "  Storm ORM: " << std::fixed << std::setprecision(2) << storm_throughput << "M rows/sec" << std::endl;
    std::cout << "  Raw SQLite: " << std::fixed << std::setprecision(2) << raw_throughput << "M rows/sec" << std::endl;
    std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << efficiency << "%" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Medium: 4 conditions with AND/OR
void benchmark_where_medium(int num_people, int iterations = 100) {
    setup_database(num_people);

    QuerySet<TestPerson> person_qs;
    BenchmarkTimer       timer;
    double               storm_time = 0;
    int                  storm_rows = 0;

    // Storm ORM
    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = person_qs
                              .where((field<^^TestPerson::age>() > 25 and field<^^TestPerson::salary>() > 40000.0) or
                                     (field<^^TestPerson::name>().like("Person5%") and
                                      field<^^TestPerson::is_active>() == true))
                              .select();
        person_qs.reset();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            storm_rows += result.value().size();
            storm_time += elapsed;
        }
    }

    // Raw SQLite
    auto&       conn = QuerySet<TestPerson>::get_default_connection();
    std::string sql  = "SELECT id, name, age, salary, is_active FROM TestPerson "
                       "WHERE (age > ? AND salary > ?) OR (name LIKE ? AND is_active = ?)";

    timer.reset();
    double raw_time = 0;
    int    raw_rows = 0;

    // Prepare statement once outside loop
    auto stmt_result = conn.prepare(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_database();
        return;
    }
    auto stmt = std::move(stmt_result.value());

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        stmt.reset(); // Reset statement, don't re-prepare
        stmt.bind_int(1, 25);
        stmt.bind_double(2, 40000.0);
        stmt.bind_text(3, "Person5%");
        stmt.bind_int(4, 1);

        std::vector<TestPerson> results;
        results.reserve(num_people);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                TestPerson p;
                p.id        = stmt.extract_int(0);
                p.name      = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                p.age       = stmt.extract_int(2);
                p.salary    = stmt.extract_double(3);
                p.is_active = stmt.extract_int(4) != 0;
                results.push_back(std::move(p));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        raw_rows += results.size();
        raw_time += elapsed;
    }

    double storm_throughput = storm_rows / (storm_time / 1000.0) / 1000000.0;
    double raw_throughput   = raw_rows / (raw_time / 1000.0) / 1000000.0;
    double efficiency       = (storm_throughput / raw_throughput) * 100.0;

    std::cout << "WHERE (medium: 4 conditions with AND/OR):" << std::endl;
    std::cout << "  Storm ORM: " << std::fixed << std::setprecision(2) << storm_throughput << "M rows/sec" << std::endl;
    std::cout << "  Raw SQLite: " << std::fixed << std::setprecision(2) << raw_throughput << "M rows/sec" << std::endl;
    std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << efficiency << "%" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

// Complex: 8+ conditions with nested AND/OR and special methods
void benchmark_where_complex(int num_people, int iterations = 100) {
    setup_database(num_people);

    QuerySet<TestPerson> person_qs;
    BenchmarkTimer       timer;
    double               storm_time = 0;
    int                  storm_rows = 0;

    // Storm ORM
    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        auto result = person_qs
                              .where(((field<^^TestPerson::age>().between(25, 40) and
                                       field<^^TestPerson::salary>() > 35000.0) or
                                      (field<^^TestPerson::name>().like("Person1%") and
                                       field<^^TestPerson::is_active>() == true)) and
                                     (field<^^TestPerson::id>() < 500 or
                                      (field<^^TestPerson::age>() > 50 and field<^^TestPerson::salary>() < 80000.0)))
                              .select();
        person_qs.reset();
        double elapsed = timer.elapsed_ms();

        if (result.has_value()) {
            storm_rows += result.value().size();
            storm_time += elapsed;
        }
    }

    // Raw SQLite
    auto&       conn = QuerySet<TestPerson>::get_default_connection();
    std::string sql  = "SELECT id, name, age, salary, is_active FROM TestPerson "
                       "WHERE ((age BETWEEN ? AND ? AND salary > ?) OR (name LIKE ? AND is_active = ?)) "
                       "AND (id < ? OR (age > ? AND salary < ?))";

    timer.reset();
    double raw_time = 0;
    int    raw_rows = 0;

    // Prepare statement once outside loop
    auto stmt_result = conn.prepare(sql);
    if (!stmt_result.has_value()) {
        std::cerr << "Failed to prepare statement" << std::endl;
        teardown_database();
        return;
    }
    auto stmt = std::move(stmt_result.value());

    for (int i = 0; i < iterations; ++i) {
        timer.reset();
        stmt.reset(); // Reset statement, don't re-prepare
        stmt.bind_int(1, 25);
        stmt.bind_int(2, 40);
        stmt.bind_double(3, 35000.0);
        stmt.bind_text(4, "Person1%");
        stmt.bind_int(5, 1);
        stmt.bind_int(6, 500);
        stmt.bind_int(7, 50);
        stmt.bind_double(8, 80000.0);

        std::vector<TestPerson> results;
        results.reserve(num_people);

        while (true) {
            int step = stmt.step_raw();
            if (step == decltype(stmt)::ROW_AVAILABLE) {
                TestPerson p;
                p.id        = stmt.extract_int(0);
                p.name      = std::string(reinterpret_cast<const char*>(stmt.extract_text_ptr(1)));
                p.age       = stmt.extract_int(2);
                p.salary    = stmt.extract_double(3);
                p.is_active = stmt.extract_int(4) != 0;
                results.push_back(std::move(p));
            } else if (step == decltype(stmt)::NO_MORE_ROWS) {
                break;
            } else {
                std::cerr << "Step failed" << std::endl;
                break;
            }
        }

        double elapsed = timer.elapsed_ms();
        raw_rows += results.size();
        raw_time += elapsed;
    }

    double storm_throughput = storm_rows / (storm_time / 1000.0) / 1000000.0;
    double raw_throughput   = raw_rows / (raw_time / 1000.0) / 1000000.0;
    double efficiency       = (storm_throughput / raw_throughput) * 100.0;

    std::cout << "WHERE (complex: 8+ conditions, nested AND/OR, special methods):" << std::endl;
    std::cout << "  Storm ORM: " << std::fixed << std::setprecision(2) << storm_throughput << "M rows/sec" << std::endl;
    std::cout << "  Raw SQLite: " << std::fixed << std::setprecision(2) << raw_throughput << "M rows/sec" << std::endl;
    std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) << efficiency << "%" << std::endl;
    std::cout << std::endl;

    teardown_database();
}

//==============================================================================
// MAIN
//==============================================================================

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --size=N          Number of rows (default: 10000)" << std::endl;
    std::cout << "  --iterations=N    Number of iterations (default: 100)" << std::endl;
    std::cout << std::endl;
    std::cout << "Benchmark Categories:" << std::endl;
    std::cout << "  --baseline        Run baseline (no WHERE)" << std::endl;
    std::cout << "  --types           Run data type benchmarks (int, string, float, bool)" << std::endl;
    std::cout << "  --special         Run special method benchmarks (LIKE, BETWEEN, IN)" << std::endl;
    std::cout << "  --complex         Run complex query benchmarks (simple, medium, complex)" << std::endl;
    std::cout << "  --all             Run all benchmarks (default)" << std::endl;
    std::cout << std::endl;
    std::cout << "  --help, -h        Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    int test_size  = 10000;
    int iterations = 100;

    // Benchmark category flags
    bool run_baseline = false;
    bool run_types    = false;
    bool run_special  = false;
    bool run_complex  = false;
    bool run_all      = true; // Default: run everything

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--size=", 7) == 0) {
            test_size = std::stoi(argv[i] + 7);
        } else if (strncmp(argv[i], "--iterations=", 13) == 0) {
            iterations = std::stoi(argv[i] + 13);
        } else if (strcmp(argv[i], "--baseline") == 0) {
            run_baseline = true;
            run_all      = false;
        } else if (strcmp(argv[i], "--types") == 0) {
            run_types = true;
            run_all   = false;
        } else if (strcmp(argv[i], "--special") == 0) {
            run_special = true;
            run_all     = false;
        } else if (strcmp(argv[i], "--complex") == 0) {
            run_complex = true;
            run_all     = false;
        } else if (strcmp(argv[i], "--all") == 0) {
            run_all = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    std::cout << "====================================================" << std::endl;
    std::cout << "   Storm ORM WHERE Comprehensive Benchmark" << std::endl;
    std::cout << "====================================================" << std::endl;
    std::cout << std::endl;

    std::cout << "Testing with " << test_size << " rows (iterations: " << iterations << ")" << std::endl;
    std::cout << std::string(60, '-') << std::endl;
    std::cout << std::endl;

    // Baseline
    if (run_all || run_baseline) {
        std::cout << "BASELINE:" << std::endl;
        std::cout << std::string(60, '-') << std::endl;
        benchmark_select_no_where(test_size, iterations);
    }

    // Data types
    if (run_all || run_types) {
        std::cout << "DATA TYPE COMPARISONS:" << std::endl;
        std::cout << std::string(60, '-') << std::endl;
        benchmark_where_int(test_size, iterations);
        benchmark_where_string(test_size, iterations);
        benchmark_where_float(test_size, iterations);
        benchmark_where_bool(test_size, iterations);
    }

    // Special methods
    if (run_all || run_special) {
        std::cout << "SPECIAL METHODS:" << std::endl;
        std::cout << std::string(60, '-') << std::endl;
        benchmark_where_like(test_size, iterations);
        benchmark_where_between(test_size, iterations);

        benchmark_where_in_3_ct(test_size, iterations);
        benchmark_where_in_10_ct(test_size, iterations);
    }

    // Complex queries
    if (run_all || run_complex) {
        std::cout << "COMPLEX QUERIES:" << std::endl;
        std::cout << std::string(60, '-') << std::endl;
        benchmark_where_simple(test_size, iterations);
        benchmark_where_medium(test_size, iterations);
        benchmark_where_complex(test_size, iterations);
    }

    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Benchmark complete!" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    return 0;
}
