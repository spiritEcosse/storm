#pragma once

/**
 * @file test_models.h
 * @brief Test-specific helpers + re-export of shared model structs.
 *
 * IMPORTANT: Include this file AFTER `import storm;` in each .cpp that uses it.
 * The [[= storm::meta::FieldAttr::*]] attributes require the storm module to be
 * imported before these structs are compiled.
 *
 * Usage in test files:
 *   import storm;
 *   #include "test_models.h"  // AFTER import
 */

// Shared model structs (Person, SimpleRecord, Message, ExtendedTypes, Task, Color,
// Indexes<Person>, PEOPLE_25, MESSAGES_8) — also used by benchmarks.
#include "../shared/models.h"

#include <gtest/gtest.h>
#include <span>

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

// Variadic helper — creates tables for all given model types.
// Returns true only if all tables were created successfully.
template <typename ConnType, typename... Models>
inline auto ensure_tables(const std::shared_ptr<ConnType> &conn) -> bool {
    return (ensure_table<Models, ConnType>(conn).has_value() && ...);
}

// Populates join test data: 3 Persons + 5 Messages with sender FKs.
// Used by DistinctTest, ValuesTest, and AggregateTest fixtures.
template <typename ConnType> inline auto populate_join_test_data() -> void {
    storm::QuerySet<Person, ConnType> person_qs;
    std::vector<Person> const people = {
        {.name = "Alice", .age = 30},
        {.name = "Bob", .age = 25},
        {.name = "Charlie", .age = 35},
    };
    auto person_result = person_qs.insert(std::span<const Person>(people)).execute();
    if (!person_result.has_value()) {
        ADD_FAILURE() << "populate_join_test_data: person insert failed: " << person_result.error().message();
        return;
    }

    storm::QuerySet<Message, ConnType> msg_qs;
    std::vector<Message> const messages = {
        {.content = "Hello", .sender = {.id = 1}},    {.content = "World", .sender = {.id = 1}},
        {.content = "Hi there", .sender = {.id = 2}}, {.content = "Goodbye", .sender = {.id = 2}},
        {.content = "Test", .sender = {.id = 3}},
    };
    auto msg_result = msg_qs.insert(std::span<const Message>(messages)).execute();
    if (!msg_result.has_value()) {
        ADD_FAILURE() << "populate_join_test_data: message insert failed: " << msg_result.error().message();
    }
}

// ============================================================================
// Model record generators -- used by InsertRunner/UpdateRunner/EraseRunner
// ============================================================================

template <typename Model> auto make_record(int i) -> Model = delete;
template <> inline auto make_record<Person>(int i) -> Person {
    return {.name = std::format("P{}", i + 1),
            .age = 20 + (i % 50),
            .salary = 1000.0 * (i + 1),
            .is_active = (i % 2 == 0),
            .years_experience = i % 30};
}
template <> inline auto make_record<SimpleRecord>(int i) -> SimpleRecord { return {0, std::format("R{}", i), i}; }
template <> inline auto make_record<Message>(int i) -> Message {
    return {.content = std::format("M{}", i), .value = i};
}

template <typename Model> auto make_updated_record(const Model &) -> Model = delete;
template <> inline auto make_updated_record<Person>(const Person &p) -> Person {
    Person u = p;
    u.name = std::format("Updated{}", p.id);
    return u;
}
template <> inline auto make_updated_record<SimpleRecord>(const SimpleRecord &r) -> SimpleRecord {
    return {r.id, std::format("Updated{}", r.id), r.value * 2};
}

template <typename Model> auto is_original_record(const Model &) -> bool = delete;
template <> inline auto is_original_record<Person>(const Person &p) -> bool { return p.name.starts_with("P"); }
template <> inline auto is_original_record<SimpleRecord>(const SimpleRecord &r) -> bool {
    return r.name.starts_with("R");
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
 * Usage — multi-table (no override needed):
 *   template <typename ConnType>
 *   class JoinTest : public StormTestFixture<Person, ConnType, Message> {};
 *
 * Usage — additional setup after table creation (seeding, QS init):
 *   template <typename ConnType>
 *   class MyTest : public StormTestFixture<Person, ConnType> {
 *   protected:
 *     auto on_after_setup(const std::shared_ptr<ConnType>& conn) -> void override {
 *       // seed data, create QuerySet members, etc.
 *     }
 *   };
 *
 * Note: the Model type only determines which QuerySet<> holds the default connection.
 * Because Storm uses a per-ConnType shared connection, any QuerySet<*, ConnType>
 * within the test will use the same underlying database.
 */
template <typename Model, typename ConnType, typename... ExtraModels> class StormTestFixture : public ::testing::Test {
  public:
    using connection_type = std::shared_ptr<ConnType>;

  protected:
    // Handles the universal SetUp: connection → pg_schema_init → on_setup → on_after_setup.
    auto SetUp() -> void override {
        if (!setup_connection()) {
            GTEST_SKIP() << "Backend unavailable";
            return;
        }
        const auto &conn = storm::QuerySet<Model, ConnType>::get_default_connection(); // NOSONAR(S1659)
        storm::test::pg_schema_init<ConnType>(conn);
        on_setup(conn);
        if (StormTestFixture::HasFatalFailure())
            return;
        on_after_setup(conn);
    }

    // Default: create tables for Model + ExtraModels.
    // Override only for fixtures that need completely custom table creation.
    virtual auto on_setup(const std::shared_ptr<ConnType> &conn) -> void {
        ASSERT_TRUE((storm::test::ensure_tables<ConnType, Model, ExtraModels...>(conn))) << "Failed to create table(s)";
    }

    // Hook for additional setup after primary table creation succeeds.
    // Override to create extra tables, seed data, or initialize QuerySet members.
    // No need to call base or check HasFatalFailure() — SetUp() handles that.
    virtual auto on_after_setup(const std::shared_ptr<ConnType> & /*conn*/) -> void {
        // Default: no additional setup. Override in fixtures that need seeding or extra initialization.
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
        auto result = // NOSONAR(S1659)
            storm::QuerySet<Model, ConnType>::set_default_connection(storm::test::get_connection_string<ConnType>());
        return result.has_value();
    }
};
