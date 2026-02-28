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

// Shared simple record — covers batch/transaction/update/reset tests needing {id, name, value}.
struct SimpleRecord {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
    int value{};
};

// Shared Message model — covers FK join tests. Sender is a Person.
struct Message {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string content;
    int value{};
    [[= storm::meta::FieldAttr::fk]] Person sender;
};

// =============================================================================
// Section 1: Type coverage structs (no FK dependencies)
// =============================================================================

// Extended numeric types model — covers int64_t, float, double, unsigned int,
// long long, optional<double>, optional<int64_t>, and a label string.
// Limited to 8 non-PK fields to stay within constexpr SSO (22-char placeholder limit).
// Replaces: IntTypes, FloatTypes, MixedTypes, OptTypes, DataTypes, UnsignedTypes,
// LongLongTypes, FloatType, OptionalDouble, OptionalInt64.
struct ExtendedTypes {
    [[= storm::meta::FieldAttr::primary]] int id{};
    int64_t big_num{};
    double precise{};
    float approx{};
    unsigned int u_int{};
    long long ll_signed{};
    std::optional<double> opt_double;
    std::optional<int64_t> opt_int64;
    std::string label;
};

// Author model — covers reflection/meta tests with many field types.
struct Author {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::string name;
    int age;
    std::string email;
    bool is_active;
    double rating;
    float score;
    std::string middleName;
    std::string biography;
};

// Coverage-gap models — CovPerson with department field for GROUP BY tests.
struct CovPerson {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
    int age{};
    std::string department;
    double salary{};
};

// Unique constraint model — covers SQLite error tests.
struct UniqueTestPerson {
    [[= storm::meta::FieldAttr::primary]] int id{};
    [[= storm::meta::FieldAttr::unique]] std::string email;
    int value{};
};

struct Measurement {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string sensor_name;
    float temperature{};
    long long timestamp{};
};

struct Counter {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
    long count{};
};

// =============================================================================
// Section 2: FK-dependent structs (must come after section 1)
// =============================================================================

struct NullableFKMessage {
    [[= storm::meta::FieldAttr::primary]] int id{};
    [[= storm::meta::FieldAttr::fk]] std::optional<Person> sender;
    [[= storm::meta::FieldAttr::fk]] Person receiver;
    std::string text;
};

struct Project {
    [[= storm::meta::FieldAttr::primary]] int id{};
    [[= storm::meta::FieldAttr::fk]] Person manager;
    std::string title;
    double budget{};
};

struct Task {
    [[= storm::meta::FieldAttr::primary]] int id{};
    [[= storm::meta::FieldAttr::fk]] Person assignee;
    [[= storm::meta::FieldAttr::fk]] Person reviewer;
    std::string description;
};

struct Reading {
    [[= storm::meta::FieldAttr::primary]] int id{};
    [[= storm::meta::FieldAttr::fk]] Measurement measurement;
    std::string reading_type;
    float value{};
};

struct Summary {
    [[= storm::meta::FieldAttr::primary]] int id{};
    [[= storm::meta::FieldAttr::fk]] Counter counter;
    std::string report_type;
};

// =============================================================================

// SQL CREATE TABLE statements — generated at runtime from C++26 reflection.

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
inline const std::string &person_create_sql = storm::create_table_sql<Person>();
inline const std::string &message_create_sql = storm::create_table_sql<Message>();
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

namespace storm::test {

// Type-safe CREATE TABLE IF NOT EXISTS using SchemaStatement.
// pg_schema_init is called once in StormTestFixture::SetUp before on_setup — not here.
template <typename T, typename ConnType> inline auto ensure_table(auto &conn) {
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
 * @brief Base fixture for typed ORM tests — template method pattern.
 *
 * IMPORTANT: This class references storm::QuerySet and must only be parsed AFTER
 * `import storm;`. Since test_models.h is included after the import in all test
 * files, this is safe here.
 *
 * Provides a universal SetUp/TearDown cycle:
 *   SetUp():    setup_connection() → pg_schema_init (once) → on_setup(conn) [virtual hook]
 *   TearDown(): rollback PG schema → clear connection
 *
 * Usage — zero SetUp (single table, no seeding):
 *   template <typename ConnType>
 *   class MyTest : public StormTestFixture<Person, ConnType> {};
 *
 * Usage — seeding or multi-table:
 *   template <typename ConnType>
 *   class MyTest : public StormTestFixture<Person, ConnType> {
 *   protected:
 *     auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
 *       StormTestFixture<Person, ConnType>::on_setup(conn); // creates Person table + txn
 *       if (HasFatalFailure()) return;
 *       ASSERT_TRUE((storm::test::ensure_table<Message, ConnType>(conn).has_value()));
 *       // seed data ...
 *     }
 *   };
 *
 * For multi-table fixtures, override on_setup completely:
 *   auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
 *       ASSERT_TRUE((storm::test::ensure_table<Person, ConnType>(conn).has_value()));
 *       ASSERT_TRUE((storm::test::ensure_table<Message, ConnType>(conn).has_value()));
 *       // seed data ...
 *   }
 *
 * Note: the Model type only determines which QuerySet<> holds the default connection.
 * Because Storm uses a per-ConnType shared connection, any QuerySet<*, ConnType>
 * within the test will use the same underlying database.
 */
template <typename Model, typename ConnType> class StormTestFixture : public ::testing::Test {
  public:
    using connection_type = std::shared_ptr<ConnType>;

  protected:
    // Handles the universal SetUp: connection check → pg_schema_init (once) → on_setup().
    auto SetUp() -> void override {
        if (!setup_connection()) {
            GTEST_SKIP() << "Backend unavailable";
            return;
        }
        const auto &conn = storm::QuerySet<Model, ConnType>::get_default_connection();
        storm::test::pg_schema_init<ConnType>(conn);
        on_setup(conn);
    }

    // Default: create primary model table.
    // Override for multi-table fixtures or test-data seeding.
    virtual auto on_setup(const std::shared_ptr<ConnType> &conn) -> void {
        ASSERT_TRUE((storm::test::ensure_table<Model, ConnType>(conn).has_value())) << "Failed to create table";
    }

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
