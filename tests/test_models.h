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

// SQL CREATE TABLE statements — generated at runtime from C++26 reflection.

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
inline const std::string &person_create_sql = storm::create_table_sql<Person>();
inline const std::string &message_create_sql = storm::create_table_sql<Message>();
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

namespace storm::test {

// Type-safe CREATE TABLE IF NOT EXISTS using SchemaStatement.
// For PostgreSQL, initialises the per-process test schema on the first call.
// Call this AFTER set_default_connection() so the default connection is available.
template <typename T, typename ConnType> inline auto ensure_table(auto &conn) {
    pg_schema_init<ConnType>(conn);
    return storm::orm::schema::SchemaStatement<T>::create_table_if_not_exists(conn);
}

// Populates join test data: 3 Persons + 5 Messages with sender FKs.
// Used by DistinctTest, ValuesTest, and AggregateTest fixtures.
template <typename ConnType> inline void populate_join_test_data() {
    storm::QuerySet<Person, ConnType> person_qs;
    std::ignore = person_qs.insert(Person{.name = "Alice", .age = 30}).execute();
    std::ignore = person_qs.insert(Person{.name = "Bob", .age = 25}).execute();
    std::ignore = person_qs.insert(Person{.name = "Charlie", .age = 35}).execute();

    storm::QuerySet<Message, ConnType> msg_qs;
    std::ignore = msg_qs.insert(Message{.content = "Hello", .sender = {.id = 1}}).execute();
    std::ignore = msg_qs.insert(Message{.content = "World", .sender = {.id = 1}}).execute();
    std::ignore = msg_qs.insert(Message{.content = "Hi there", .sender = {.id = 2}}).execute();
    std::ignore = msg_qs.insert(Message{.content = "Goodbye", .sender = {.id = 2}}).execute();
    std::ignore = msg_qs.insert(Message{.content = "Test", .sender = {.id = 3}}).execute();
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
