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
    class FieldsSqlParity : public StormTestFixture<Person, Conn> {
      public:
        // NOTE on style: SonarCloud S1659 forbids multiple identifiers per declaration
        // ("QuerySet<Person, Conn> a1, a2;"), and the Storm Strict gate is
        // zero-tolerance. So each comparison takes its QuerySet from this factory
        // rather than declaring a pile of locals. where() is immutable, but a fresh
        // object per side keeps the comparison honest regardless.
        [[nodiscard]] static auto qs() -> QuerySet<Person, Conn> {
            return QuerySet<Person, Conn>{};
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

} // namespace
