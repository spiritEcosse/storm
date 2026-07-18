#include <gtest/gtest.h>
#include "test_db_helpers.h"
#include <sqlite3.h>

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954
#include "test_seed_helpers.h"

struct CompositeUpsertRecord {
    [[= storm::FieldAttr::primary]] int id{};
    std::string                         name;
    std::string                         department;
    int                                 age{};
};

template <> struct storm::Indexes<CompositeUpsertRecord> {
    using type = std::tuple<storm::UniqueIndex<^^CompositeUpsertRecord::name, ^^CompositeUpsertRecord::department>>;
};

struct TimestampedUpsertRecord {
    [[= storm::FieldAttr::primary]] int                                       id{};
    [[= storm::FieldAttr::unique]] std::string                                name;
    [[= storm::FieldAttr::auto_create]] std::chrono::system_clock::time_point created_at{};
    [[= storm::FieldAttr::auto_update]] std::chrono::system_clock::time_point updated_at{};
};

struct UniqueOwnerRecord {
    [[= storm::FieldAttr::primary]] int                    id{};
    [[= storm::FieldAttr::unique]][[= storm::fk<>]] Person owner;
    std::string                                            label;
};

namespace {
    using storm::orm::statements::UpsertGrammar;
}

// Grammar emits a single-column conflict target via the FK-aware writer.
TEST(UpsertGrammarTest, ConflictTargetSingleColumn) {
    constexpr auto    target = UpsertGrammar<Person>::build_conflict_target<^^Person::name>();
    const std::string s(target);
    EXPECT_EQ(s, "(name)");
}

// Grammar emits col=excluded.col for each listed SET column.
TEST(UpsertGrammarTest, ExcludedSetClauseMultipleColumns) {
    constexpr auto    set = UpsertGrammar<Person>::build_excluded_set_clause<^^Person::age, ^^Person::salary>();
    const std::string s(set);
    EXPECT_EQ(s, "age=excluded.age, salary=excluded.salary");
}

TEST(UpsertGrammarTest, ForeignKeyColumnsUseIdSuffix) {
    constexpr auto target = UpsertGrammar<UniqueOwnerRecord>::build_conflict_target<^^UniqueOwnerRecord::owner>();
    constexpr auto set    = UpsertGrammar<UniqueOwnerRecord>::build_excluded_set_clause<^^UniqueOwnerRecord::owner>();
    EXPECT_EQ(std::string(target), "(owner_id)");
    EXPECT_EQ(std::string(set), "owner_id=excluded.owner_id");
}

// Full DO NOTHING statement for Person conflicting on name.
TEST(UpsertGrammarTest, FullSqlDoNothing) {
    const std::string& sql = UpsertGrammar<Person>::nothing_sql<^^Person::name>();
    EXPECT_TRUE(sql.starts_with("INSERT INTO Person ")) << sql;
    EXPECT_NE(sql.find("ON CONFLICT (name) DO NOTHING"), std::string::npos) << sql;
    EXPECT_TRUE(sql.ends_with("RETURNING id")) << sql;
}

// Full DO UPDATE statement, conflict on name, set age.
TEST(UpsertGrammarTest, FullSqlDoUpdate) {
    const std::string sql = UpsertGrammar<Person>::update_sql<^^Person::name>(
            UpsertGrammar<Person>::build_excluded_set_clause<^^Person::age>()
    );
    EXPECT_NE(sql.find("ON CONFLICT (name) DO UPDATE SET age=excluded.age"), std::string::npos) << sql;
    EXPECT_TRUE(sql.ends_with("RETURNING id")) << sql;
}

// Positive: a single unique field IS a valid conflict target.
static_assert(storm::orm::statements::ConflictTargetUnique<Person, ^^Person::name>);
// Positive: the UniqueIndex<name, department> column set IS valid.
static_assert(storm::orm::statements::ConflictTargetUnique<Person, ^^Person::name, ^^Person::department>);
// Negative: a non-unique column is NOT a valid conflict target.
static_assert(!storm::orm::statements::ConflictTargetUnique<Person, ^^Person::age>);
// Negative: inserts omit the PK, so it cannot be a conflict target.
static_assert(!storm::orm::statements::ConflictTargetUnique<Person, ^^Person::id>);
// Negative: the PK is NOT settable.
static_assert(!storm::orm::statements::UpsertSettable<Person, ^^Person::id>);
// Positive: a normal column IS settable.
static_assert(storm::orm::statements::UpsertSettable<Person, ^^Person::age>);

TEST(UpsertGrammarTest, ConstraintsCompile) {
    SUCCEED();
}

template <typename ConnType> class UpsertTest : public StormTestFixture<Person, ConnType> {};
TYPED_TEST_SUITE(UpsertTest, DatabaseTypes);

template <typename ConnType> class CompositeUpsertTest : public StormTestFixture<CompositeUpsertRecord, ConnType> {};
TYPED_TEST_SUITE(CompositeUpsertTest, DatabaseTypes);

// Seeds the initial "Zed" row via the DO NOTHING proxy and returns its id.
// Shared by both conflict tests below so each only adds its own conflicting insert.
template <typename ConnType> auto seed_zed(storm::QuerySet<Person, ConnType>& qs) -> std::int64_t {
    Person const first{.name = "Zed", .age = 1, .department = "X"};
    auto         first_id = qs.insert(first).template on_conflict<^^Person::name>().nothing().execute();
    EXPECT_TRUE(first_id.has_value());
    EXPECT_TRUE(first_id.value().has_value());
    return first_id.value().value();
}

// DO NOTHING via the fluent proxy: first insert lands, the conflicting second
// insert is skipped (no row touched), leaving the original row untouched.
TYPED_TEST(UpsertTest, DoNothingSkipsOnConflict) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    seed_zed(qs);

    Person const conflicting{.name = "Zed", .age = 99, .department = "Y"};
    auto         second = qs.insert(conflicting).template on_conflict<^^Person::name>().nothing().execute();
    ASSERT_TRUE(second.has_value());
    EXPECT_FALSE(second.value().has_value()); // skipped — no row touched

    auto rows = qs.where(f<^^Person::name>() == "Zed").select().execute();
    ASSERT_TRUE(rows.has_value());
    ASSERT_EQ(rows.value().size(), 1U);
    EXPECT_EQ(rows.value().begin()->age, 1); // untouched by the conflicting insert
}

// DO UPDATE via the fluent proxy: the conflicting insert overwrites the listed
// column (age) on the existing row.
TYPED_TEST(UpsertTest, DoUpdateOverwritesListedColumn) {
    using storm::orm::where::f;
    storm::QuerySet<Person, TypeParam> qs;
    const std::int64_t                 first_id = seed_zed(qs);

    Person const conflicting{.name = "Zed", .age = 99, .department = "Y"};
    auto         updated_id =
            qs.insert(conflicting).template on_conflict<^^Person::name>().template update<^^Person::age>().execute();
    ASSERT_TRUE(updated_id.has_value());
    EXPECT_EQ(updated_id.value(), first_id);

    auto rows = qs.where(f<^^Person::name>() == "Zed").select().execute();
    ASSERT_TRUE(rows.has_value());
    ASSERT_EQ(rows.value().size(), 1U);
    EXPECT_EQ(rows.value().begin()->age, 99); // overwritten by the conflicting insert
}

// .sql() golden — DO NOTHING shape via the fluent proxy.
TYPED_TEST(UpsertTest, SqlGoldenNothing) {
    storm::QuerySet<Person, TypeParam> qs;
    Person const                       row{.name = "Q", .age = 1, .department = "X"};
    const std::string                  sql = qs.insert(row).template on_conflict<^^Person::name>().nothing().sql();
    EXPECT_NE(sql.find("ON CONFLICT (name) DO NOTHING"), std::string::npos) << sql;
}

// Composite conflict target via UniqueIndex<name, department>.
TYPED_TEST(CompositeUpsertTest, CompositeConflictTarget) {
    storm::QuerySet<CompositeUpsertRecord, TypeParam> qs;
    CompositeUpsertRecord const                       rec{.name = "Cara", .department = "Eng", .age = 20};
    auto                                              first = qs.insert(rec)
                         .template on_conflict<^^CompositeUpsertRecord::name, ^^CompositeUpsertRecord::department>()
                         .template update<^^CompositeUpsertRecord::age>()
                         .execute();
    ASSERT_TRUE(first.has_value()) << first.error().message();
    CompositeUpsertRecord const upd{.name = "Cara", .department = "Eng", .age = 21};
    auto                        second = qs.insert(upd)
                          .template on_conflict<^^CompositeUpsertRecord::name, ^^CompositeUpsertRecord::department>()
                          .template update<^^CompositeUpsertRecord::age>()
                          .execute();
    ASSERT_TRUE(second.has_value()) << second.error().message();
    EXPECT_EQ(second.value(), first.value());
}

// DO UPDATE with multiple SET columns.
TYPED_TEST(UpsertTest, DoUpdateMultipleColumns) {
    storm::QuerySet<Person, TypeParam> qs;
    Person const                       rec{.name = "Dan", .age = 10, .salary = 100.0, .department = "Eng"};
    auto                               first = qs.insert(rec)
                         .template on_conflict<^^Person::name>()
                         .template update<^^Person::age, ^^Person::salary>()
                         .execute();
    ASSERT_TRUE(first.has_value()) << first.error().message();
    Person const upd{.name = "Dan", .age = 11, .salary = 200.0, .department = "Eng"};
    auto         r = qs.insert(upd)
                     .template on_conflict<^^Person::name>()
                     .template update<^^Person::age, ^^Person::salary>()
                     .execute();
    ASSERT_TRUE(r.has_value()) << r.error().message();
    EXPECT_EQ(r.value(), first.value());
    auto row = qs.where(storm::orm::where::f<^^Person::name>() == std::string("Dan")).select().execute();
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ(row.value().begin()->age, 11);
    EXPECT_DOUBLE_EQ(row.value().begin()->salary, 200.0);
}

// to_sql() inlines the bound params (debug helper).
TYPED_TEST(UpsertTest, ToSqlInlinesParams) {
    storm::QuerySet<Person, TypeParam> qs;
    Person const                       rec{.name = "Eve", .age = 5, .department = "X"};
    auto                               r = qs.insert(rec).template on_conflict<^^Person::name>().nothing().to_sql();
    ASSERT_TRUE(r.has_value());
    EXPECT_NE(r.value().find("ON CONFLICT (name) DO NOTHING"), std::string::npos) << r.value();
}

// auto_update (#209) is re-stamped on a DO UPDATE upsert. The unique name
// forces the second insert to hit the existing row; the auto_update updated_at
// column is bound now() afresh, so the stored value advances. Timestamps are
// bind-time-only (no write-back), so re-SELECT to read.
template <typename ConnType> class UpsertTimestampTest : public StormTestFixture<TimestampedUpsertRecord, ConnType> {};
TYPED_TEST_SUITE(UpsertTimestampTest, DatabaseTypes);

TYPED_TEST(UpsertTimestampTest, DoUpdateRefreshesAutoUpdateTimestamp) {
    using storm::orm::where::f;
    storm::QuerySet<TimestampedUpsertRecord, TypeParam> qs;

    TimestampedUpsertRecord const seed{.name = "record"};
    auto                          inserted = qs.insert(seed)
                            .template on_conflict<^^TimestampedUpsertRecord::name>()
                            .template update<^^TimestampedUpsertRecord::name>()
                            .execute();
    ASSERT_TRUE(inserted.has_value());

    auto before = qs.where(f<^^TimestampedUpsertRecord::name>() == "record").select().execute();
    ASSERT_TRUE(before.has_value());
    ASSERT_EQ(before.value().size(), 1U);
    const auto first_stamp = before.value().begin()->updated_at;

    // Sleep one second: the stored format has 1s resolution (tp_to_string), so a
    // sub-second gap would round to the same string and falsely fail the advance check.
    std::this_thread::sleep_for(std::chrono::seconds(1));

    TimestampedUpsertRecord const conflicting{.name = "record"};
    auto                          updated = qs.insert(conflicting)
                           .template on_conflict<^^TimestampedUpsertRecord::name>()
                           .template update<^^TimestampedUpsertRecord::name>()
                           .execute();
    ASSERT_TRUE(updated.has_value());

    auto after = qs.where(f<^^TimestampedUpsertRecord::name>() == "record").select().execute();
    ASSERT_TRUE(after.has_value());
    ASSERT_EQ(after.value().size(), 1U);
    EXPECT_GT(after.value().begin()->updated_at, first_stamp); // updated_at advanced
}
