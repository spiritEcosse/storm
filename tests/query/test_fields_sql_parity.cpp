#include <gtest/gtest.h>
#include <meta>
#include "test_db_helpers.h"

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954

using storm::QuerySet;
using storm::orm::where::f;

namespace {

    using Conn = storm::db::sqlite::Connection;

    // Stage 1's correctness criterion: the two spellings produce BYTE-IDENTICAL SQL
    // in every selector position.
    //
    // Pinned to one backend rather than typed over DatabaseTypes: .sql() renders the
    // statement without executing it, and both spellings feed the SAME grammar, so a
    // dialect difference (PG's NULLS FIRST/LAST, LIMIT ALL) applies equally to both
    // sides of every comparison and cannot expose a proxy-vs-^^ divergence. The
    // per-backend rendering itself is already covered by test_sql_verify.cpp.
    //
    // A fixture is still required despite the tests being structural: .sql() reaches
    // get_default_connection(), which asserts when no connection is set.
    class FieldsSqlParity : public StormTestFixture<Person, Conn, Message> {
      public:
        // NOTE on style: SonarCloud S1659 forbids multiple identifiers per declaration
        // ("QuerySet<Person, Conn> a1, a2;"), and the Storm Strict gate is
        // zero-tolerance. So each comparison takes its QuerySet from this factory
        // rather than declaring a pile of locals. where() is immutable, but a fresh
        // object per side keeps the comparison honest regardless.
        [[nodiscard]] static auto qs() -> QuerySet<Person, Conn> {
            return QuerySet<Person, Conn>{};
        }
        [[nodiscard]] static auto msg_qs() -> QuerySet<Message, Conn> {
            return QuerySet<Message, Conn>{};
        }
    };

    TEST_F(FieldsSqlParity, BareComparisonOperator) {
        // THE headline spelling from the issue — no f<> wrapper.
        EXPECT_EQ(
                qs().where(f<^^Person::age>() == 30).select().sql(), qs().where(fields::Person.age == 30).select().sql()
        );
    }

    TEST_F(FieldsSqlParity, AllSixComparisonOperators) {
        // Per the CLAUDE.md testing checklist: every comparison operator.
        EXPECT_EQ(
                qs().where(f<^^Person::age>() == 30).select().sql(), qs().where(fields::Person.age == 30).select().sql()
        );
        EXPECT_EQ(
                qs().where(f<^^Person::age>() != 30).select().sql(), qs().where(fields::Person.age != 30).select().sql()
        );
        EXPECT_EQ(
                qs().where(f<^^Person::age>() > 30).select().sql(), qs().where(fields::Person.age > 30).select().sql()
        );
        EXPECT_EQ(
                qs().where(f<^^Person::age>() >= 30).select().sql(), qs().where(fields::Person.age >= 30).select().sql()
        );
        EXPECT_EQ(
                qs().where(f<^^Person::age>() < 30).select().sql(), qs().where(fields::Person.age < 30).select().sql()
        );
        EXPECT_EQ(
                qs().where(f<^^Person::age>() <= 30).select().sql(), qs().where(fields::Person.age <= 30).select().sql()
        );
    }

    TEST_F(FieldsSqlParity, CompoundExpression) {
        // The motivating case: the model is named ONCE per field, not twice.
        EXPECT_EQ(
                qs().where(f<^^Person::department>() == "Eng" && f<^^Person::age>() < 28).select().sql(),
                qs().where(fields::Person.department == "Eng" && fields::Person.age < 28).select().sql()
        );
    }

    TEST_F(FieldsSqlParity, OrAndNestedComposition) {
        EXPECT_EQ(
                qs().where((f<^^Person::age>() > 30 && f<^^Person::is_active>() == true) ||
                           f<^^Person::department>() == "Eng")
                        .select()
                        .sql(),
                qs().where((fields::Person.age > 30 && fields::Person.is_active == true) ||
                           fields::Person.department == "Eng")
                        .select()
                        .sql()
        );
    }

    TEST_F(FieldsSqlParity, InBetweenLikeCollate) {
        using storm::orm::utilities::Collate;
        EXPECT_EQ(
                qs().where(f<^^Person::id>().in(1, 2, 3)).select().sql(),
                qs().where(fields::Person.id.in(1, 2, 3)).select().sql()
        );
        EXPECT_EQ(
                qs().where(f<^^Person::age>().between(20, 40)).select().sql(),
                qs().where(fields::Person.age.between(20, 40)).select().sql()
        );
        EXPECT_EQ(
                qs().where(f<^^Person::name>().like("A%")).select().sql(),
                qs().where(fields::Person.name.like("A%")).select().sql()
        );
        EXPECT_EQ(
                qs().where(f<^^Person::name>().collate(Collate::NoCase) == "bob").select().sql(),
                qs().where(fields::Person.name.collate(Collate::NoCase) == "bob").select().sql()
        );
    }

    TEST_F(FieldsSqlParity, NullChecksOnOptionalField) {
        EXPECT_EQ(
                qs().where(f<^^Person::score>().is_null()).select().sql(),
                qs().where(fields::Person.score.is_null()).select().sql()
        );
        EXPECT_EQ(
                qs().where(f<^^Person::nickname>().is_not_null()).select().sql(),
                qs().where(fields::Person.nickname.is_not_null()).select().sql()
        );
    }

    TEST_F(FieldsSqlParity, MixedSpellingsInOneExpression) {
        // A migration leaves files half-converted; both spellings must compose.
        EXPECT_EQ(
                qs().where(f<^^Person::department>() == "Eng" && f<^^Person::age>() < 28).select().sql(),
                qs().where(fields::Person.department == "Eng" && f<^^Person::age>() < 28).select().sql()
        );
    }

    TEST_F(FieldsSqlParity, OrderBySingleAndMulti) {
        EXPECT_EQ(qs().order_by<^^Person::name>().select().sql(), qs().order_by<fields::Person.name>().select().sql());
        EXPECT_EQ(
                (qs().order_by<^^Person::department, ^^Person::age>().select().sql()),
                (qs().order_by<fields::Person.department, fields::Person.age>().select().sql())
        );
    }

    TEST_F(FieldsSqlParity, OrderByWithDescModifier) {
        // The bool modifier attaches to the field BEFORE it. The proxy must not
        // disturb that positional pairing (it writes result[idx-1]).
        EXPECT_EQ(
                (qs().order_by<^^Person::age, false>().select().sql()),
                (qs().order_by<fields::Person.age, false>().select().sql())
        );
    }

    TEST_F(FieldsSqlParity, OrderByWithCollateModifier) {
        using storm::orm::utilities::Collate;
        EXPECT_EQ(
                (qs().order_by<^^Person::name, Collate::NoCase>().select().sql()),
                (qs().order_by<fields::Person.name, Collate::NoCase>().select().sql())
        );
    }

    TEST_F(FieldsSqlParity, OrderByMixedSpellings) {
        EXPECT_EQ(
                (qs().order_by<^^Person::department, ^^Person::age>().select().sql()),
                (qs().order_by<fields::Person.department, ^^Person::age>().select().sql())
        );
    }

    TEST_F(FieldsSqlParity, OrderByCombinedWithWhereAndLimit) {
        EXPECT_EQ(
                qs().where(f<^^Person::age>() > 20).order_by<^^Person::name>().limit(5).select().sql(),
                qs().where(fields::Person.age > 20).order_by<fields::Person.name>().limit(5).select().sql()
        );
    }

    TEST_F(FieldsSqlParity, DistinctSingleAndMulti) {
        EXPECT_EQ(qs().distinct<^^Person::department>().sql(), qs().distinct<fields::Person.department>().sql());
        EXPECT_EQ(
                (qs().distinct<^^Person::department, ^^Person::age>().sql()),
                (qs().distinct<fields::Person.department, fields::Person.age>().sql())
        );
    }

    TEST_F(FieldsSqlParity, ValuesProjection) {
        EXPECT_EQ(qs().values<^^Person::name>().sql(), qs().values<fields::Person.name>().sql());
        EXPECT_EQ(
                (qs().values<^^Person::name, ^^Person::age>().sql()),
                (qs().values<fields::Person.name, fields::Person.age>().sql())
        );
    }

    TEST_F(FieldsSqlParity, ScalarAggregates) {
        EXPECT_EQ(qs().sum<^^Person::age>().sql(), qs().sum<fields::Person.age>().sql());
        EXPECT_EQ(qs().avg<^^Person::salary>().sql(), qs().avg<fields::Person.salary>().sql());
        EXPECT_EQ(qs().min<^^Person::age>().sql(), qs().min<fields::Person.age>().sql());
        EXPECT_EQ(qs().max<^^Person::age>().sql(), qs().max<fields::Person.age>().sql());
        EXPECT_EQ(qs().count<^^Person::id>().sql(), qs().count<fields::Person.id>().sql());
        EXPECT_EQ(
                qs().count_distinct<^^Person::department>().sql(),
                qs().count_distinct<fields::Person.department>().sql()
        );
    }

    TEST_F(FieldsSqlParity, MultiFieldAggregate) {
        // SUM(a + b) — the pack-order-sensitive position. A reordered or dropped pack
        // emits VALID but WRONG SQL, which a single-field assertion cannot detect.
        EXPECT_EQ(
                (qs().sum<^^Person::age, ^^Person::years_experience>().sql()),
                (qs().sum<fields::Person.age, fields::Person.years_experience>().sql())
        );
        // Pin the operand ORDER too, so a symmetric reordering bug cannot pass.
        EXPECT_TRUE((
                qs().sum<fields::Person.age, fields::Person.years_experience>().sql().contains("age + years_experience")
        ));
    }

    TEST_F(FieldsSqlParity, AggregateStatementChainMethods) {
        // AggregateStatement's OWN chain methods — a separate code path from
        // GroupByBuilder's identically-named ones, and the one the other assertions miss.
        EXPECT_EQ(qs().sum<^^Person::age>().count().sql(), qs().sum<fields::Person.age>().count().sql());
        EXPECT_EQ(
                qs().sum<^^Person::age>().avg<^^Person::salary>().sql(),
                qs().sum<fields::Person.age>().avg<fields::Person.salary>().sql()
        );
        EXPECT_EQ(
                qs().min<^^Person::age>().max<^^Person::age>().sql(),
                qs().min<fields::Person.age>().max<fields::Person.age>().sql()
        );
    }

    TEST_F(FieldsSqlParity, GroupByWithAggregate) {
        EXPECT_EQ(
                qs().group_by<^^Person::department>().count<>().sql(),
                qs().group_by<fields::Person.department>().count<>().sql()
        );
        EXPECT_EQ(
                qs().group_by<^^Person::department>().sum<^^Person::salary>().sql(),
                qs().group_by<fields::Person.department>().sum<fields::Person.salary>().sql()
        );
        // Multi-column GROUP BY — the other pack-order-sensitive position.
        EXPECT_EQ(
                (qs().group_by<^^Person::department, ^^Person::age>().count<>().sql()),
                (qs().group_by<fields::Person.department, fields::Person.age>().count<>().sql())
        );
    }

    TEST_F(FieldsSqlParity, GroupByHavingBothChainPositions) {
        // Both chaining orders, per the CLAUDE.md testing checklist.
        EXPECT_EQ(
                qs().group_by<^^Person::age>().having(f<^^Person::age>() > 30).count<>().sql(),
                qs().group_by<fields::Person.age>().having(fields::Person.age > 30).count<>().sql()
        );
        EXPECT_EQ(
                qs().group_by<^^Person::department>().count<>().having(f<^^Person::department>() == "Eng").sql(),
                qs().group_by<fields::Person.department>().count<>().having(fields::Person.department == "Eng").sql()
        );
    }

    TEST_F(FieldsSqlParity, ConditionalUpdateAndUpdateAll) {
        // Write paths return std::expected<std::string, Error>. Assert has_value()
        // first so a failure reports the SQL, not two opaque errors.
        const Person proto{.salary = 60000, .is_active = true};

        const auto legacy_upd =
                qs().where(f<^^Person::salary>() < 50000).update<^^Person::salary, ^^Person::is_active>(proto).to_sql();
        const auto proxy_upd = qs().where(fields::Person.salary < 50000)
                                       .update<fields::Person.salary, fields::Person.is_active>(proto)
                                       .to_sql();
        ASSERT_TRUE(legacy_upd.has_value());
        ASSERT_TRUE(proxy_upd.has_value());
        EXPECT_EQ(*legacy_upd, *proxy_upd);

        const auto legacy_all = qs().update_all<^^Person::department>(proto).to_sql();
        const auto proxy_all  = qs().update_all<fields::Person.department>(proto).to_sql();
        ASSERT_TRUE(legacy_all.has_value());
        ASSERT_TRUE(proxy_all.has_value());
        EXPECT_EQ(*legacy_all, *proxy_all);
    }

    TEST_F(FieldsSqlParity, JoinOnFkField) {
        EXPECT_EQ(
                msg_qs().join<^^Message::sender>().select().sql(),
                msg_qs().join<fields::Message.sender>().select().sql()
        );
        EXPECT_EQ(
                msg_qs().left_join<^^Message::sender>().select().sql(),
                msg_qs().left_join<fields::Message.sender>().select().sql()
        );
    }

    // An FK member IS a persisted column and MUST appear in the generated struct —
    // only m2m/reverse_fk containers are filtered out.
    template <typename FieldsT> constexpr bool has_sender = requires(const FieldsT& obj) { obj.sender; };
    static_assert(has_sender<decltype(fields::Message)>);

    // The numeric-aggregate gate survives the widening in BOTH spellings. A dropped
    // pack constraint is otherwise silently green.
    template <auto S> constexpr bool summable = requires(QuerySet<Person, Conn> qs) { qs.template sum<S>(); };

    static_assert(summable<^^Person::age>);        // int — allowed
    static_assert(summable<fields::Person.age>);   // same, via the proxy
    static_assert(!summable<^^Person::name>);      // std::string — rejected
    static_assert(!summable<fields::Person.name>); // MUST also be rejected via the proxy

    // Upsert is a write path: to_sql() returns std::expected<std::string, Error>.
    // A helper keeps each case to the has_value + compare shape without repetition.
    [[nodiscard]] auto sql_of(auto&& proxy) -> std::string {
        auto result = proxy.to_sql();
        EXPECT_TRUE(result.has_value());
        return result.value_or(std::string{});
    }

    TEST_F(FieldsSqlParity, UpsertOnConflictDoUpdate) {
        const Person p{.id = 1, .name = "Ann", .age = 30};
        EXPECT_EQ(
                sql_of(qs().insert(p).on_conflict<^^Person::name>().update<^^Person::age>()),
                sql_of(qs().insert(p).on_conflict<fields::Person.name>().update<fields::Person.age>())
        );
    }

    TEST_F(FieldsSqlParity, UpsertOnConflictDoNothing) {
        const Person p{.id = 1, .name = "Ann", .age = 30};
        EXPECT_EQ(
                sql_of(qs().insert(p).on_conflict<^^Person::name>().nothing()),
                sql_of(qs().insert(p).on_conflict<fields::Person.name>().nothing())
        );
    }

    TEST_F(FieldsSqlParity, UpsertMultiColumnSetClause) {
        const Person p{.id = 1, .name = "Ann", .age = 30};
        EXPECT_EQ(
                sql_of(qs().insert(p).on_conflict<^^Person::name>().update<^^Person::age, ^^Person::salary>()),
                sql_of(qs().insert(p)
                               .on_conflict<fields::Person.name>()
                               .update<fields::Person.age, fields::Person.salary>())
        );
    }

    // The conflict target must still be a unique column in BOTH spellings.
    template <auto S>
    constexpr bool conflictable =
            requires(QuerySet<Person, Conn> qs, Person obj) { qs.insert(obj).template on_conflict<S>(); };

    static_assert(conflictable<^^Person::name>);      // [[= storm::unique]] — allowed
    static_assert(conflictable<fields::Person.name>); // same, via the proxy
    static_assert(!conflictable<^^Person::age>);      // not unique — rejected
    static_assert(!conflictable<fields::Person.age>); // MUST also be rejected via the proxy

} // namespace
