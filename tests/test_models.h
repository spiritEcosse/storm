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

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Shared Person model — covers id/name/age tests, salary/experience aggregates,
// is_active ordering, optional score/nickname, and avatar BLOB.
struct Person {
    [[= storm::meta::FieldAttr::primary]] int id{};
    [[= storm::meta::FieldAttr::unique]] std::string name;
    int age{};
    double salary{};
    bool is_active{};
    int years_experience{};
    [[= storm::meta::FieldAttr::indexed]] std::string department;
    std::optional<int> score;
    std::optional<std::string> nickname;
    std::vector<uint8_t> avatar;
};

// Composite indexes for Person — specialize the trait after struct definition
template <> struct storm::Indexes<Person> {
    using type = std::tuple<storm::Index<^^Person::department, ^^Person::age>,
                            storm::UniqueIndex<^^Person::name, ^^Person::department>>;
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

// Enum type for testing enum field support
enum class Color : int { Red = 0, Green = 1, Blue = 2 };

// Extended types model — covers all supported SQLite column types.
// Includes: int64, float, double, unsigned, long long, char types, enum,
// chrono date/datetime/duration, filesystem::path, UUID, vector<byte>,
// plus optional variants.
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
    signed char tiny_signed{};
    unsigned char tiny_unsigned{};
    char single_char{};
    Color color{Color::Red};
    std::chrono::year_month_day date_field{};
    std::chrono::system_clock::time_point datetime_field{};
    std::chrono::seconds duration_field{};
    std::filesystem::path file_path;
    std::vector<std::byte> raw_data;
    storm::UUID uuid_field;
    std::optional<Color> opt_color;
    std::optional<std::chrono::system_clock::time_point> opt_timestamp;
    std::optional<std::filesystem::path> opt_path;
};

// =============================================================================
// Section 2: FK-dependent structs (must come after section 1)
// =============================================================================

struct Task {
    [[= storm::meta::FieldAttr::primary]] int id{};
    [[= storm::meta::FieldAttr::fk]] Person assignee;
    [[= storm::meta::FieldAttr::fk]] Person reviewer;
    std::string description;
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

// Variadic helper — creates tables for all given model types.
// Returns true only if all tables were created successfully.
template <typename ConnType, typename... Models> inline bool ensure_tables(const std::shared_ptr<ConnType> &conn) {
    return (ensure_table<Models, ConnType>(conn).has_value() && ...);
}

// Populates join test data: 3 Persons + 5 Messages with sender FKs.
// Used by DistinctTest, ValuesTest, and AggregateTest fixtures.
template <typename ConnType> inline void populate_join_test_data() {
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
// Model record generators -- used by InsertRunner/UpdateRunner/RemoveRunner
// ============================================================================

template <typename Model> Model make_record(int i) = delete;
template <> inline Person make_record<Person>(int i) {
    return {.name = std::format("P{}", i + 1),
            .age = 20 + (i % 50),
            .salary = 1000.0 * (i + 1),
            .is_active = (i % 2 == 0),
            .years_experience = i % 30};
}
template <> inline SimpleRecord make_record<SimpleRecord>(int i) { return {0, std::format("R{}", i), i}; }
template <> inline Message make_record<Message>(int i) { return {.content = std::format("M{}", i), .value = i}; }

template <typename Model> Model make_updated_record(const Model &) = delete;
template <> inline Person make_updated_record<Person>(const Person &p) {
    Person u = p;
    u.name = std::format("Updated{}", p.id);
    return u;
}
template <> inline SimpleRecord make_updated_record<SimpleRecord>(const SimpleRecord &r) {
    return {r.id, std::format("Updated{}", r.id), r.value * 2};
}

template <typename Model> bool is_original_record(const Model &) = delete;
template <> inline bool is_original_record<Person>(const Person &p) { return p.name.starts_with("P"); }
template <> inline bool is_original_record<SimpleRecord>(const SimpleRecord &r) { return r.name.starts_with("R"); }

// ---------------------------------------------------------------------------
// Unified seed dataset — ONE shared 25-row Person + 8-row Message dataset.
// Used by both C++ fixtures and YAML tests.
// ---------------------------------------------------------------------------

// clang-format off
inline constexpr std::array<Person, 25> PEOPLE_25 = {
    Person{.name = "Alice",   .age = 25, .salary = 55000.0, .is_active = true,  .years_experience = 5,  .department = "Engineering", .score = std::optional<int>(85),      .nickname = std::optional<std::string>("Ali")},
    Person{.name = "Bob",     .age = 30, .salary = 62000.0, .is_active = true,  .years_experience = 10, .department = "Sales",       .score = std::optional<int>(90),      .nickname = std::optional<std::string>("Bobby")},
    Person{.name = "Charlie", .age = 35, .salary = 78000.0, .is_active = false, .years_experience = 15, .department = "Marketing",   .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Diana",   .age = 28, .salary = 48000.0, .is_active = true,  .years_experience = 5,  .department = "HR",          .score = std::optional<int>(75),      .nickname = std::optional<std::string>("Di")},
    Person{.name = "Eve",     .age = 40, .salary = 92000.0, .is_active = false, .years_experience = 10, .department = "Engineering", .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Frank",   .age = 45, .salary = 88000.0, .is_active = true,  .years_experience = 15, .department = "Sales",       .score = std::optional<int>(60),      .nickname = std::nullopt},
    Person{.name = "Grace",   .age = 25, .salary = 52000.0, .is_active = true,  .years_experience = 5,  .department = "Marketing",   .score = std::optional<int>(95),      .nickname = std::optional<std::string>("Gracie")},
    Person{.name = "Henry",   .age = 33, .salary = 70000.0, .is_active = false, .years_experience = 10, .department = "Support",     .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Ivy",     .age = 30, .salary = 65000.0, .is_active = true,  .years_experience = 5,  .department = "Engineering", .score = std::optional<int>(80),      .nickname = std::optional<std::string>("Iv")},
    Person{.name = "Jack",    .age = 38, .salary = 85000.0, .is_active = false, .years_experience = 15, .department = "HR",          .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Karen",   .age = 25, .salary = 50000.0, .is_active = true,  .years_experience = 5,  .department = "Sales",       .score = std::optional<int>(85),      .nickname = std::optional<std::string>("Kiki")},
    Person{.name = "Leo",     .age = 42, .salary = 95000.0, .is_active = true,  .years_experience = 10, .department = "Engineering", .score = std::optional<int>(70),      .nickname = std::nullopt},
    Person{.name = "Mia",     .age = 28, .salary = 46000.0, .is_active = true,  .years_experience = 5,  .department = "Marketing",   .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Nick",    .age = 35, .salary = 72000.0, .is_active = false, .years_experience = 15, .department = "Support",     .score = std::optional<int>(55),      .nickname = std::optional<std::string>("Nicky")},
    Person{.name = "Olivia",  .age = 48, .salary = 98000.0, .is_active = true,  .years_experience = 10, .department = "Sales",       .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Paul",    .age = 22, .salary = 32000.0, .is_active = false, .years_experience = 5,  .department = "HR",          .score = std::optional<int>(40),      .nickname = std::nullopt},
    Person{.name = "Quinn",   .age = 30, .salary = 67000.0, .is_active = true,  .years_experience = 10, .department = "Engineering", .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Rachel",  .age = 36, .salary = 76000.0, .is_active = false, .years_experience = 5,  .department = "Support",     .score = std::optional<int>(65),      .nickname = std::optional<std::string>("Rach")},
    Person{.name = "Sam",     .age = 40, .salary = 90000.0, .is_active = true,  .years_experience = 15, .department = "Marketing",   .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Tina",    .age = 27, .salary = 44000.0, .is_active = true,  .years_experience = 5,  .department = "Sales",       .score = std::optional<int>(88),      .nickname = std::optional<std::string>("T")},
    Person{.name = "Uma",     .age = 33, .salary = 69000.0, .is_active = false, .years_experience = 10, .department = "HR",          .score = std::optional<int>(50),      .nickname = std::nullopt},
    Person{.name = "Victor",  .age = 45, .salary = 93000.0, .is_active = true,  .years_experience = 15, .department = "Engineering", .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Wendy",   .age = 29, .salary = 58000.0, .is_active = true,  .years_experience = 10, .department = "Support",     .score = std::optional<int>(78),      .nickname = std::optional<std::string>("Wen")},
    Person{.name = "Xander",  .age = 38, .salary = 82000.0, .is_active = false, .years_experience = 15, .department = "Marketing",   .score = std::nullopt,                .nickname = std::nullopt},
    Person{.name = "Yara",    .age = 22, .salary = 35000.0, .is_active = true,  .years_experience = 5,  .department = "Support",     .score = std::optional<int>(92),      .nickname = std::optional<std::string>("Yari")},
};
// clang-format on

// 8 Messages — sender IDs are placeholders (1-4); for PostgreSQL, re-query after insert.
inline constexpr std::array<Message, 8> MESSAGES_8 = {
    Message{.content = "Hello", .value = 10, .sender = {.id = 1}},
    Message{.content = "World", .value = 20, .sender = {.id = 1}},
    Message{.content = "Hi there", .value = 30, .sender = {.id = 1}},
    Message{.content = "Goodbye", .value = 40, .sender = {.id = 2}},
    Message{.content = "Testing", .value = 50, .sender = {.id = 2}},
    Message{.content = "Greetings", .value = 60, .sender = {.id = 3}},
    Message{.content = "Reply", .value = 70, .sender = {.id = 3}},
    Message{.content = "Quick note", .value = 80, .sender = {.id = 4}},
};

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
