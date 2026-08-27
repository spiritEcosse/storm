#pragma once

// Shared test body for test_fk_column_name_orderby_aggregate_sqlite.cpp / test_fk_column_name_orderby_aggregate_pg.cpp
// — the two single-backend TUs of a compile-time TU split (see test_db_helpers.h,
// DatabaseTypesSqliteHalf/DatabaseTypesPgHalf). Splitting a 2-backend TU into
// two lets ninja compile them in parallel instead of serially instantiating
// both backends in one TU; keeping the body here (instead of duplicating it
// into both .cpp files) removes the risk of the two halves silently drifting.
//
// The includer must #define STORM_SPLIT_TYPES / STORM_SPLIT_TYPE_NAMES to one
// backend's ::testing::Types<> alias / NameGenerator before #include-ing this
// file, and #undef both afterward. Never include this file directly.
#if !defined(STORM_SPLIT_TYPES) || !defined(STORM_SPLIT_TYPE_NAMES)
#error "test_fk_column_name_orderby_aggregate_body.h: define STORM_SPLIT_TYPES/STORM_SPLIT_TYPE_NAMES before including"
#endif

// NOLINTBEGIN(misc-const-correctness)

import storm;
import std;
// SingleColumnSelector — the COUNT(DISTINCT) gate; not re-exported by `storm`
// (same reason test_aggregateable_concept.cpp imports this module directly).
import storm_orm_statements_aggregate;

#include "test_models.h" // NOSONAR cpp:S954

// Must follow test_models.h: Shipment / LedgerEntryRef name storm:: annotations.
// Supplies the composite-PK FK targets the compile-time gate below is asserted on.
#include "crud/test_composite_pk_models.h" // NOSONAR cpp:S954

using storm::QuerySet;

namespace {

// Message::sender is an FK to Person → column `sender_id`.
// Person rows must exist first: the FK constraint is enforced on both backends.
template <typename ConnType> class FkColumnNameTest : public StormTestFixture<Person, ConnType, Message> {
  public:
    // Seeds 3 Persons and 5 Messages whose senders are, in order:
    // p1, p1, p2, p2, p3 — so COUNT(DISTINCT sender_id) == 3 while COUNT(*) == 5,
    // making the DISTINCT genuinely load-bearing (a plain COUNT would give 5).
    auto on_after_setup(const std::shared_ptr<ConnType> &conn) -> void override {
        QuerySet<Person, ConnType> person_qs;
        const std::vector<Person> people = {
            Person{.name = "Ann", .age = 30},
            Person{.name = "Bob", .age = 40},
            Person{.name = "Cid", .age = 50},
        };
        auto inserted = person_qs.insert(people).execute();
        ASSERT_TRUE(inserted.has_value()) << "Failed to seed Person rows";

        // Re-query: PostgreSQL assigns its own ids, so the seeded literals
        // above cannot be assumed (same pattern as MESSAGES_8's comment).
        auto stored = person_qs.template order_by<fields::Person.id>().select().execute();
        ASSERT_TRUE(stored.has_value()) << "Failed to read back Person rows";
        ASSERT_EQ(stored->size(), 3U);

        std::vector<int> ids;
        ids.reserve(3);
        for (const auto &p : *stored) {
            ids.push_back(p.id);
        }
        person_ids_ = {ids[0], ids[1], ids[2]};

        QuerySet<Message, ConnType> msg_qs;
        const std::vector<Message> messages = {
            Message{.content = "m1", .value = 10, .sender = {.id = ids[0]}},
            Message{.content = "m2", .value = 20, .sender = {.id = ids[0]}},
            Message{.content = "m3", .value = 30, .sender = {.id = ids[1]}},
            Message{.content = "m4", .value = 40, .sender = {.id = ids[1]}},
            Message{.content = "m5", .value = 50, .sender = {.id = ids[2]}},
        };
        ASSERT_TRUE(msg_qs.insert(messages).execute().has_value()) << "Failed to seed Message rows";
        (void)conn;
    }

    std::array<int, 3> person_ids_{};
};
TYPED_TEST_SUITE(FkColumnNameTest, STORM_SPLIT_TYPES, STORM_SPLIT_TYPE_NAMES);

// ── ORDER BY on an FK member ────────────────────────────────────────────────

// The core reproduction: before the fix this emitted `ORDER BY sender` and
// failed to prepare ("no such column: sender") on both backends.
TYPED_TEST(FkColumnNameTest, OrderByFkMemberExecutes) {
    QuerySet<Message, TypeParam> qs;
    auto result = qs.template order_by<fields::Message.sender>().select().execute();
    ASSERT_TRUE(result.has_value()) << "ORDER BY on an FK member failed: " << result.error().message();
    ASSERT_EQ(result->size(), 5U);

    // Ordered by sender_id ASC — the FK column's value, not the member identifier.
    std::vector<int> senders;
    senders.reserve(5);
    for (const auto &m : *result) {
        senders.push_back(m.sender.id);
    }
    EXPECT_TRUE(std::ranges::is_sorted(senders)) << "Rows are not ordered by sender_id";
    EXPECT_EQ(senders.front(), this->person_ids_[0]);
    EXPECT_EQ(senders.back(), this->person_ids_[2]);
}

// DESC must order by the same column, reversed — proves the direction suffix
// still attaches to the rewritten column name rather than to a stale identifier.
TYPED_TEST(FkColumnNameTest, OrderByFkMemberDescExecutes) {
    QuerySet<Message, TypeParam> qs;
    auto result = qs.template order_by<fields::Message.sender, false>().select().execute();
    ASSERT_TRUE(result.has_value()) << "ORDER BY DESC on an FK member failed: " << result.error().message();
    ASSERT_EQ(result->size(), 5U);

    std::vector<int> senders;
    senders.reserve(5);
    for (const auto &m : *result) {
        senders.push_back(m.sender.id);
    }
    EXPECT_TRUE(std::ranges::is_sorted(senders, std::greater<>{})) << "Rows are not ordered by sender_id DESC";
    EXPECT_EQ(senders.front(), this->person_ids_[2]);
}

// Mixed pack: a non-FK member and an FK member in one ORDER BY. Guards the
// per-field dispatch — the writer must rewrite ONLY the FK entry.
TYPED_TEST(FkColumnNameTest, OrderByMixedFkAndPlainMemberExecutes) {
    QuerySet<Message, TypeParam> qs;
    auto result = qs.template order_by<fields::Message.sender, fields::Message.value>().select().execute();
    ASSERT_TRUE(result.has_value()) << "Mixed ORDER BY failed: " << result.error().message();
    ASSERT_EQ(result->size(), 5U);

    auto it = result->begin();
    EXPECT_EQ(it->value, 10);
    EXPECT_EQ(it->sender.id, this->person_ids_[0]);
}

// SQL text: the emitted ORDER BY names sender_id, and nothing spells the bare
// `sender` as a column. (Execution above is the real gate; this pins the text.)
TYPED_TEST(FkColumnNameTest, OrderByFkMemberSqlNamesFkColumn) {
    QuerySet<Message, TypeParam> qs;
    const auto sql = qs.template order_by<fields::Message.sender>().select().sql();
    EXPECT_TRUE(sql.contains("ORDER BY sender_id")) << "Emitted SQL: " << sql;
}

// ── COUNT(DISTINCT <fk>) ────────────────────────────────────────────────────

// Before the fix: "no such column: sender".
TYPED_TEST(FkColumnNameTest, CountDistinctFkMemberExecutes) {
    QuerySet<Message, TypeParam> qs;
    auto result = qs.template count_distinct<fields::Message.sender>().execute();
    ASSERT_TRUE(result.has_value()) << "COUNT(DISTINCT fk) failed: " << result.error().message();
    // 5 messages, 3 distinct senders — DISTINCT is load-bearing here.
    EXPECT_EQ(*result, 3);
}

TYPED_TEST(FkColumnNameTest, CountDistinctFkMemberWithWhereExecutes) {
    QuerySet<Message, TypeParam> qs;
    auto result = qs.where(fields::Message.value > 25).template count_distinct<fields::Message.sender>().execute();
    ASSERT_TRUE(result.has_value()) << "COUNT(DISTINCT fk) + WHERE failed: " << result.error().message();
    // Messages m3 (p2), m4 (p2), m5 (p3) survive → 2 distinct senders.
    EXPECT_EQ(*result, 2);
}

TYPED_TEST(FkColumnNameTest, CountDistinctFkMemberSqlNamesFkColumn) {
    QuerySet<Message, TypeParam> qs;
    const auto sql = qs.template count_distinct<fields::Message.sender>().sql();
    EXPECT_TRUE(sql.contains("COUNT(DISTINCT sender_id)")) << "Emitted SQL: " << sql;
}

// ── GROUP BY over an FK member (aggregate.cppm:172 — the third writer) ──────

// GROUP BY's column list shares the same writer bug. It is reachable when the
// GROUP BY key is a plain column and the FK appears only as the COUNT(DISTINCT)
// target; grouping BY an FK member itself does not compile (the row extractor
// has no extract_column_value<Person>, extract.cppm), so this is the reachable
// half. Asserted so the corrected writer is exercised end to end.
TYPED_TEST(FkColumnNameTest, GroupByPlainWithCountDistinctFkExecutes) {
    QuerySet<Message, TypeParam> qs;
    auto result =
        qs.template group_by<fields::Message.value>().template count_distinct<fields::Message.sender>().execute();
    ASSERT_TRUE(result.has_value()) << "GROUP BY + COUNT(DISTINCT fk) failed: " << result.error().message();
    // Each of the 5 messages has a unique `value`, so every group holds 1 row
    // and therefore exactly 1 distinct sender.
    ASSERT_EQ(result->size(), 5U);
    for (const auto &row : *result) {
        EXPECT_EQ(std::get<1>(row), 1);
    }
}

// ── Composite-PK FK target is rejected at compile time ──────────────────────
//
// An FK whose target has an N-part composite PK spreads over "<member>_<part>"
// columns — there is NO "<member>_id" for it (#504). Routing such a member through
// the single-column writer would emit `ORDER BY line_id` / `COUNT(DISTINCT line_id)`,
// naming a column no table has: the #570 failure one shape further out.
//
// Rejected rather than expanded, because neither clause has a correct multi-column
// form. ORDER BY's direction/COLLATE suffix attaches once, after ONE column name, so
// a comma list would apply ASC/DESC to the LAST part only — valid SQL, wrong order,
// no diagnostic. And COUNT(DISTINCT a, b) is a SYNTAX ERROR in SQLite while
// PostgreSQL parses it as a row constructor, so expansion would be backend-divergent.
//
// Asserted on the gate predicates, not via the ill-formed calls: a TU cannot contain
// the ill-formed call it asserts about (same pattern as #502's ReturnIdSupported).

// Message::sender → single-PK Person: one column (`sender_id`), accepted.
static_assert(storm::meta::is_single_column_member(storm::meta::selector_info<fields::Message.sender>()),
              "an FK to a single-PK target is one column and stays usable in ORDER BY / COUNT(DISTINCT)");
// A plain non-FK column is trivially one column.
static_assert(storm::meta::is_single_column_member(storm::meta::selector_info<fields::Person.age>()),
              "an ordinary column is always single-column");
// Shipment::line → 2-part composite PK (order_id, product_id): rejected.
static_assert(!storm::meta::is_single_column_member(storm::meta::selector_info<fields::Shipment.line>()),
              "an FK to a composite-PK target spans several columns and has no <member>_id");
static_assert(storm::meta::column_span_of(storm::meta::selector_info<fields::Shipment.line>()) == 2,
              "Shipment::line spans exactly the target's two PK parts");

// The gate is not arity-limited — it counts the target's parts rather than testing
// for exactly two. Asserted on the reflection directly, since LedgerEntryRef (whose
// `ledger` FK targets the 3-part Ledger) has no fields:: selector declared.
static_assert(storm::meta::column_span_of(^^LedgerEntryRef::ledger) == 3,
              "a 3-part composite FK target spans three columns");
static_assert(!storm::meta::is_single_column_member(^^LedgerEntryRef::ledger), "and is rejected the same way");

// The gates themselves, not just the predicate under them — a correct predicate that
// was never wired into a constraint would still ship the bug. Both are asserted, so
// deleting either requires-clause fails the build rather than silently reopening #570.
static_assert(storm::orm::statements::OrderBySelectorsAreSingleColumn<fields::Message.sender>,
              "ORDER BY <single-PK fk> stays available");
static_assert(!storm::orm::statements::OrderBySelectorsAreSingleColumn<fields::Shipment.line>,
              "ORDER BY <composite fk> is refused at the call site");
// The modifier args (bool / Collate) must not trip the per-arg concept. Load-bearing:
// selector_info is constrained by AnySelector and does not normalise for a bool, so
// this pins that is_field_arg short-circuits before it is ever reached.
static_assert(storm::orm::statements::OrderBySelectorsAreSingleColumn<fields::Person.age, false>,
              "a DESC modifier is not a field arg and must pass the gate");

// The COUNT(DISTINCT) gate (aggregate.cppm), the sibling of the ORDER BY one above.
static_assert(storm::orm::statements::SingleColumnSelector<fields::Message.sender>,
              "COUNT(DISTINCT <single-PK fk>) stays available");
static_assert(!storm::orm::statements::SingleColumnSelector<fields::Shipment.line>,
              "COUNT(DISTINCT <composite fk>) is refused at the call site");
static_assert(!storm::orm::statements::SingleColumnSelector<fields::OptionalShipment.line>,
              "and the nullable composite form is refused too");

// ── The gates are WIRED, not merely defined ─────────────────────────────────
//
// The assertions above test the concepts; they all still hold if someone deletes the
// requires-clause that USES one, which would silently reopen #570 (mutation-verified:
// removing both clauses reintroduces `COUNT(DISTINCT line_id)` with the whole suite
// still green). These assert the CALL EXPRESSION instead, which is what actually
// fails when a gate is unwired. A requires-expression can name an ill-formed call
// without the TU containing it, so no negative-compile file is needed.
template <typename Qs, auto S>
concept CanOrderBy = requires(Qs qs) { qs.template order_by<S>(); };
template <typename Qs, auto S>
concept CanCountDistinct = requires(Qs qs) { qs.template count_distinct<S>(); };

using MessageQs = QuerySet<Message, storm::db::sqlite::Connection>;
using ShipmentQs = QuerySet<Shipment, storm::db::sqlite::Connection>;

static_assert(CanOrderBy<MessageQs, fields::Message.sender>, "ORDER BY on a single-PK FK must still compile");
static_assert(!CanOrderBy<ShipmentQs, fields::Shipment.line>,
              "QuerySet::order_by must REJECT a composite-PK FK — fails if the requires-clause is dropped");
static_assert(CanCountDistinct<MessageQs, fields::Message.sender>,
              "COUNT(DISTINCT) on a single-PK FK must still compile");
static_assert(!CanCountDistinct<ShipmentQs, fields::Shipment.line>,
              "QuerySet::count_distinct must REJECT a composite-PK FK — fails if the requires-clause is dropped");

// ── Non-FK regression: SQL must stay byte-identical ─────────────────────────

// The ~50 in-tree models are non-FK; the rewrite must not touch a single byte
// of their emitted SQL. These pin the exact strings for a plain column at each
// of the corrected writers.
template <typename ConnType> class NonFkSqlParityTest : public StormTestFixture<Person, ConnType> {};

TYPED_TEST_SUITE(NonFkSqlParityTest, STORM_SPLIT_TYPES, STORM_SPLIT_TYPE_NAMES);

TYPED_TEST(NonFkSqlParityTest, OrderByPlainMemberSqlUnchanged) {
    QuerySet<Person, TypeParam> qs;
    const auto asc = storm::test::is_postgresql<TypeParam>() ? "ASC NULLS FIRST" : "ASC";
    const auto desc = storm::test::is_postgresql<TypeParam>() ? "DESC NULLS LAST" : "DESC";

    EXPECT_EQ(qs.template order_by<fields::Person.name>().select().sql(),
              std::format("SELECT id, name, age, salary, is_active, years_experience, department, score, nickname,"
                          " avatar FROM Person ORDER BY name {}",
                          asc));
    // Bound to a local first: the comma inside order_by<..., false> would be
    // parsed as a macro-argument separator by EXPECT_EQ.
    const auto desc_sql = qs.template order_by<fields::Person.age, false>().select().sql();
    EXPECT_EQ(desc_sql,
              std::format("SELECT id, name, age, salary, is_active, years_experience, department, score, nickname,"
                          " avatar FROM Person ORDER BY age {}",
                          desc));
}

TYPED_TEST(NonFkSqlParityTest, AggregateOnPlainMemberSqlUnchanged) {
    QuerySet<Person, TypeParam> qs;
    EXPECT_EQ(qs.template count_distinct<fields::Person.department>().sql(),
              "SELECT COUNT(DISTINCT department) FROM Person");
    EXPECT_EQ(qs.template sum<fields::Person.age>().sql(), "SELECT SUM(age) FROM Person");
    // Two-field SUM: bound to a local so the comma is not a macro separator.
    const auto sum_two = qs.template sum<fields::Person.age, fields::Person.years_experience>().sql();
    EXPECT_EQ(sum_two, "SELECT SUM(age + years_experience) FROM Person");
    EXPECT_EQ(qs.template avg<fields::Person.salary>().sql(), "SELECT AVG(salary) FROM Person");
    EXPECT_EQ(qs.template min<fields::Person.age>().sql(), "SELECT MIN(age) FROM Person");
    EXPECT_EQ(qs.template max<fields::Person.age>().sql(), "SELECT MAX(age) FROM Person");
}

TYPED_TEST(NonFkSqlParityTest, GroupByPlainMemberSqlUnchanged) {
    QuerySet<Person, TypeParam> qs;
    EXPECT_EQ(qs.template group_by<fields::Person.department>().count().sql(),
              "SELECT department, COUNT(*) FROM Person GROUP BY department");
    // Bound to a local first: the comma inside group_by<..., ...> would be
    // parsed as a macro-argument separator by EXPECT_EQ.
    const auto two_key_sql = qs.template group_by<fields::Person.department, fields::Person.age>().count().sql();
    EXPECT_EQ(two_key_sql, "SELECT department, age, COUNT(*) FROM Person GROUP BY department, age");
}

} // namespace

// NOLINTEND(misc-const-correctness)
