#pragma once

/**
 * @file test_fixture.h
 * @brief Model-agnostic TYPED_TEST fixture + table-creation helpers.
 *
 * Carries no model of its own, so a TU that needs the fixture (most of them)
 * no longer parses the shared model set to get it — include the per-model
 * headers it actually uses from shared/models/ alongside this (issue #634).
 *
 * IMPORTANT: Include this file AFTER `import storm;` in each .cpp that uses it
 * — StormTestFixture references storm::QuerySet, and ensure_table references
 * storm::orm::schema::SchemaStatement.
 *
 * It also needs test_db_helpers.h, which — unlike this header — must be included
 * BEFORE `import storm;`: it forward-declares storm::db::*::Connection in the
 * global module, and after the import that is "declaration in the global module
 * follows declaration in module storm_db_sqlite". So this header cannot include
 * it, and asserts it instead. Every TU already includes it in the right place;
 * the #error only fires for a new TU that forgets, turning an obscure module
 * diagnostic into an actionable one.
 */

#ifndef STORM_TESTS_TEST_DB_HELPERS_H
#error                                                                                                                 \
    "test_fixture.h: #include \"test_db_helpers.h\" BEFORE `import storm;` (it forward-declares Connection in the global module)"
#endif

#include <gtest/gtest.h>

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

} // namespace storm::test

/**
 * @brief Base fixture for typed ORM tests — template method pattern.
 *
 * IMPORTANT: This class references storm::QuerySet and must only be parsed AFTER
 * `import storm;`. Since test_fixture.h is included after the import in all test
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
