#pragma once

/**
 * @file test_models.h
 * @brief Shared test model structs for Storm ORM test suite.
 *
 * IMPORTANT: Include this file AFTER `import storm;` in each .cpp that uses it.
 * The [[= storm::meta::FieldAttr::*]] attributes require the storm module to be
 * imported before these structs are compiled.
 *
 * Usage in test files:
 *   import storm;
 *   #include "test_models.h"  // AFTER import
 */

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Shared Person model — covers id/name/age tests, salary/experience aggregates,
// is_active ordering, optional score/nickname, and avatar BLOB.
struct Person {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
    int age{};
    double salary{};
    bool is_active{};
    int years_experience{};
    std::optional<int> score;
    std::optional<std::string> nickname;
    std::vector<uint8_t> avatar;
};

// Shared Message model — covers FK join tests. Sender is a Person.
struct Message {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string content;
    int value{};
    [[= storm::meta::FieldAttr::fk]] Person sender;
};

// SQL CREATE TABLE statements (SQLite syntax; adapt_schema<ConnType>() converts for PG).

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

inline constexpr const char *person_create_sql = "CREATE TABLE Person ("
                                                 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                                 "name TEXT NOT NULL, "
                                                 "age INTEGER NOT NULL, "
                                                 "salary REAL NOT NULL, "
                                                 "is_active INTEGER NOT NULL, "
                                                 "years_experience INTEGER NOT NULL, "
                                                 "score INTEGER, "
                                                 "nickname TEXT, "
                                                 "avatar BLOB)";

inline constexpr const char *message_create_sql = "CREATE TABLE Message ("
                                                  "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                                  "content TEXT NOT NULL, "
                                                  "value INTEGER NOT NULL, "
                                                  "sender_id INTEGER NOT NULL)";

// NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

namespace storm::test {
// Populates join test data: 3 Persons + 5 Messages with sender FKs.
// Used by DistinctTest, ValuesTest, and AggregateTest fixtures.
template <typename ConnType> inline void populate_join_test_data() {
    const auto &conn = storm::QuerySet<Person, ConnType>::get_default_connection();
    std::ignore = conn->execute(
        "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Alice', 30, 0, 0, 0)");
    std::ignore = conn->execute(
        "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Bob', 25, 0, 0, 0)");
    std::ignore = conn->execute(
        "INSERT INTO Person (name, age, salary, is_active, years_experience) VALUES ('Charlie', 35, 0, 0, 0)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('Hello', 0, 1)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('World', 0, 1)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('Hi there', 0, 2)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('Goodbye', 0, 2)");
    std::ignore = conn->execute("INSERT INTO Message (content, value, sender_id) VALUES ('Test', 0, 3)");
}
} // namespace storm::test

/**
 * @brief Base fixture for typed ORM tests — provides universal TearDown + setup_connection().
 *
 * IMPORTANT: This class references storm::QuerySet and must only be parsed AFTER
 * `import storm;`. Since test_models.h is included after the import in all test
 * files, this is safe here.
 *
 * Usage:
 *   template <typename ConnType>
 *   class MyTest : public StormTestFixture<Person, ConnType> {
 *   protected:
 *     auto SetUp() -> void override {
 *       if (!this->setup_connection()) { GTEST_SKIP() << "Backend unavailable"; return; }
 *       // create table + seed data ...
 *     }
 *     // TearDown() is inherited — clears connection, drops PG schema
 *   };
 *
 * Note: the Model type only determines which QuerySet<> holds the default connection.
 * Because Storm uses a per-ConnType shared connection, any QuerySet<*, ConnType>
 * within the test will use the same underlying database.
 */
template <typename Model, typename ConnType> class StormTestFixture : public ::testing::Test {
  protected:
    // Universal TearDown — rolls back PG schema and clears the default connection.
    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (storm::QuerySet<Model, ConnType>::has_default_connection()) {
                storm::test::rollback_test_txn<ConnType>(storm::QuerySet<Model, ConnType>::get_default_connection());
            }
        }
        storm::QuerySet<Model, ConnType>::clear_default_connection();
    }

    // Sets the default connection for this Model/ConnType.
    // Returns false if the backend is unavailable (caller should GTEST_SKIP()).
    auto setup_connection() -> bool {
        if (!storm::test::backend_available<ConnType>())
            return false;
        auto result =
            storm::QuerySet<Model, ConnType>::set_default_connection(storm::test::get_connection_string<ConnType>());
        return result.has_value();
    }
};
