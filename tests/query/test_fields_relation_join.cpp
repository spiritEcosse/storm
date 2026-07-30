#include <gtest/gtest.h>
#include "test_db_helpers.h"
#include "plf_hive/plf_hive.h"

import storm;
import std;

using storm::QuerySet;

#include "test_models.h"            // NOSONAR cpp:S954
#include "test_m2m_models.h"        // NOSONAR cpp:S954
#include "test_reverse_fk_models.h" // NOSONAR cpp:S954

namespace {

    using Conn = storm::db::sqlite::Connection;

    // A relation member is not a column, but IS a legal join target. These assert
    // the spelling `join<fields::Student.courses>()` exists and emits byte-identical
    // SQL to the ^^ form — the 65 in-tree m2m/reverse-FK join sites depend on it.
    class FieldsRelationJoin : public StormTestFixture<Student, Conn, Course, Pupil, RfPerson, RfTask> {
      public:
        [[nodiscard]] static auto student_qs() -> QuerySet<Student, Conn> {
            return QuerySet<Student, Conn>{};
        }
        [[nodiscard]] static auto pupil_qs() -> QuerySet<Pupil, Conn> {
            return QuerySet<Pupil, Conn>{};
        }
        [[nodiscard]] static auto rf_qs() -> QuerySet<RfPerson, Conn> {
            return QuerySet<RfPerson, Conn>{};
        }
    };

    TEST_F(FieldsRelationJoin, AutoJunctionM2MJoin) {
        EXPECT_EQ(
                student_qs().join<^^Student::courses>().select().sql(),
                student_qs().join<fields::Student.courses>().select().sql()
        );
    }

    TEST_F(FieldsRelationJoin, AutoJunctionM2MLeftJoin) {
        EXPECT_EQ(
                student_qs().left_join<^^Student::courses>().select().sql(),
                student_qs().left_join<fields::Student.courses>().select().sql()
        );
    }

    TEST_F(FieldsRelationJoin, ThroughModelM2MJoin) {
        // many_to_many_through<Enrollment> — the explicit-junction spelling.
        EXPECT_EQ(
                pupil_qs().join<^^Pupil::courses>().select().sql(),
                pupil_qs().join<fields::Pupil.courses>().select().sql()
        );
    }

    TEST_F(FieldsRelationJoin, ReverseFkJoin) {
        // reverse_fk goes through the same RelationRef path as m2m, but its Q2
        // hits the owner table directly (no junction) — asserted separately so
        // the claim is tested rather than inferred from the m2m cases.
        EXPECT_EQ(
                rf_qs().join<^^RfPerson::tasks>().select().sql(), rf_qs().join<fields::RfPerson.tasks>().select().sql()
        );
        EXPECT_EQ(
                rf_qs().left_join<^^RfPerson::tasks>().select().sql(),
                rf_qs().left_join<fields::RfPerson.tasks>().select().sql()
        );
    }

    TEST_F(FieldsRelationJoin, RelationJoinCombinedWithWhereAndOrderBy) {
        // The relation proxy must compose with column proxies in one chain.
        EXPECT_EQ(
                student_qs()
                        .join<^^Student::courses>()
                        .where(fields::Student.age > 20)
                        .order_by<^^Student::name>()
                        .select()
                        .sql(),
                student_qs()
                        .join<fields::Student.courses>()
                        .where(fields::Student.age > 20)
                        .order_by<fields::Student.name>()
                        .select()
                        .sql()
        );
    }

} // namespace
