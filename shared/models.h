#pragma once

/**
 * @file models.h
 * @brief Shared model structs used by both tests and benchmarks.
 *
 * IMPORTANT: Include this file AFTER `import storm;` — the
 * [[= storm::*]] attributes require the storm module, and the fields:: blocks
 * near the bottom call storm::field_specs_for, a *function*, which is a harder
 * dependency than an annotation. Including this header too early now fails
 * inside a consteval block rather than at the attribute.
 */

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <meta>
#include <optional>
#include <string>
#include <vector>

// =============================================================================
// Section 1: Core models (no FK dependencies)
// =============================================================================

// Shared Person model — covers id/name/age tests, salary/experience aggregates,
// is_active ordering, optional score/nickname, and avatar BLOB.
struct Person {
    [[= storm::primary]] int id{};
    [[= storm::unique]] std::string name;
    int age{};
    double salary{};
    bool is_active{};
    int years_experience{};
    [[= storm::indexed]] std::string department;
    std::optional<int> score;
    std::optional<std::string> nickname;
    std::vector<uint8_t> avatar;

    // Composite indexes — nested-typedef opt-in (issue #464). This header is
    // textually included in several module TUs; an explicit Indexes<Person>
    // specialization would exist once per TU and trip clang-p2996's cross-BMI
    // declaration merging (ambiguous Indexes<Person>::type).
    using storm_indexes = std::tuple<storm::Index<^^Person::department, ^^Person::age>,
                                     storm::UniqueIndex<^^Person::name, ^^Person::department>>;
};

// Shared simple record — covers batch/transaction/update/reset tests needing {id, name, value}.
struct SimpleRecord {
    [[= storm::primary]] int id{};
    std::string name;
    int value{};
};

// Shared Message model — covers FK join tests. Sender is a Person.
struct Message {
    [[= storm::primary]] int id{};
    std::string content;
    int value{};
    [[= storm::fk<>]] Person sender;
};

// =============================================================================
// Section 2: Type coverage structs (no FK dependencies)
// =============================================================================

// Enum type for testing enum field support
enum class Color : int { Red = 0, Green = 1, Blue = 2 };

// Extended types model — covers all supported SQLite column types.
struct ExtendedTypes {
    [[= storm::primary]] int id{};
    int64_t big_num{};
    double precise{};
    float approx{};
    unsigned int u_int{};
    long long ll_signed{};
    [[= storm::signed_storage]] std::uint64_t big_unsigned{};     // signed int64 storage (#419/#436)
    [[= storm::full_unsigned]] std::uint64_t big_unsigned_full{}; // order-preserving full-range storage (#436)
    std::optional<double> opt_double;
    std::optional<int64_t> opt_int64;
    std::string label;
    signed char tiny_signed{};
    unsigned char tiny_unsigned{};
    char single_char{};
    Color color{Color::Red};
    std::chrono::year_month_day date_field{std::chrono::year{2000} / std::chrono::January / std::chrono::day{1}};
    std::chrono::system_clock::time_point datetime_field{};
    std::chrono::seconds duration_field{};
    std::filesystem::path file_path;
    std::vector<std::byte> raw_data;
    storm::UUID uuid_field;
    std::optional<Color> opt_color;
    std::optional<std::chrono::system_clock::time_point> opt_timestamp;
    std::optional<std::filesystem::path> opt_path;
};

// Auto-timestamp model — created_at stamped on INSERT only, updated_at on
// INSERT and UPDATE. Both are std::chrono::system_clock::time_point (#209).
struct TimestampedRecord {
    [[= storm::primary]] int id{};
    std::string name;
    [[= storm::auto_create]] std::chrono::system_clock::time_point created_at{};
    [[= storm::auto_update]] std::chrono::system_clock::time_point updated_at{};
};

// UUID primary key model — tests DDL generation for UUID PKs (#507).
// UUID PKs are client-generated (not DB-generated like AUTOINCREMENT).
struct UuidPkModel {
    [[= storm::primary]] storm::UUID id = storm::UUID::generate();
    std::string name;
};

// =============================================================================
// Section 3: FK-dependent structs (must come after section 1)
// =============================================================================

struct Task {
    [[= storm::primary]] int id{};
    [[= storm::fk<>]] Person assignee;
    [[= storm::fk<>]] Person reviewer;
    std::string description;
};

// Per-FK ON DELETE policy models (#431). The FK annotation fk<RefAction::...> carries
// the ON DELETE policy; the schema generator emits the matching "ON DELETE <action>"
// clause. SET NULL requires a nullable FK (std::optional).
struct CascadeChild {
    [[= storm::primary]] int id{};
    [[= storm::fk<storm::RefAction::Cascade>]] Person owner;
    std::string label;
};

struct SetNullChild {
    [[= storm::primary]] int id{};
    [[= storm::fk<storm::RefAction::SetNull>]] std::optional<Person> owner;
    std::string label;
};

struct RestrictChild {
    [[= storm::primary]] int id{};
    [[= storm::fk<storm::RefAction::Restrict>]] Person owner;
    std::string label;
};

// Foreign key to UUID-PK model — tests DDL generation for FK columns referencing UUID PKs (#507).
struct UuidPkRef {
    [[= storm::primary]] int id{};
    [[= storm::fk<>]] UuidPkModel owner;
    std::string value;
};

// =============================================================================
// fields:: selector proxies (#518)
// =============================================================================
// Two mechanical lines per model, no field names — so they cannot drift when a
// model gains or loses a member. Declared for the models this header's consumers
// actually select on; a model with no selector call sites needs no proxy.

namespace fields {

struct PersonT;
consteval { std::meta::define_aggregate(^^PersonT, storm::field_specs_for(^^Person)); }
inline constexpr PersonT Person{};

struct SimpleRecordT;
consteval { std::meta::define_aggregate(^^SimpleRecordT, storm::field_specs_for(^^SimpleRecord)); }
inline constexpr SimpleRecordT SimpleRecord{};

struct MessageT;
consteval { std::meta::define_aggregate(^^MessageT, storm::field_specs_for(^^Message)); }
inline constexpr MessageT Message{};

struct TimestampedRecordT;
consteval { std::meta::define_aggregate(^^TimestampedRecordT, storm::field_specs_for(^^TimestampedRecord)); }
inline constexpr TimestampedRecordT TimestampedRecord{};

struct ExtendedTypesT;
consteval { std::meta::define_aggregate(^^ExtendedTypesT, storm::field_specs_for(^^ExtendedTypes)); }
inline constexpr ExtendedTypesT ExtendedTypes{};

} // namespace fields

// =============================================================================
// Section 4: Seed datasets
// =============================================================================

namespace storm::test {

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
