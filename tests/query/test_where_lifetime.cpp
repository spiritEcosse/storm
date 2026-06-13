#include <gtest/gtest.h>
#include "test_db_helpers.h"

// NOLINTBEGIN(misc-const-correctness,performance-unnecessary-value-param,performance-unnecessary-copy-initialization)

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954
#include "test_seed_helpers.h"

using storm::QuerySet;
using storm::orm::where::Expr;
using storm::orm::where::f;

// Lifetime safety for WHERE comparison operands (#352).
//
// QuerySet::where() is immutable/Django-style and deferred: an expression node can outlive the
// buffer a text operand points at, and .select() binds the operand only later. ComparisonExpr used
// to keep std::string_view / const char* operands by view, so a deferred bind read a dangling
// buffer (use-after-free under ASAN). Text operands are now normalized to an owning std::string at
// construction, so any deferred bind is safe.
// Run the deferred WHERE and assert it returns exactly one Bob — the operand must have been copied,
// not stored as a view into a buffer that has already been freed.
template <typename ConnType> auto expect_single_bob(const Expr& expr) -> void {
    QuerySet<Person, ConnType> queryset;
    auto                       result = queryset.where(expr).select().execute();
    ASSERT_TRUE(result.has_value()) << "Deferred bind failed: " << result.error().message();
    ASSERT_EQ(result.value().size(), 1) << "Expected exactly 1 person named Bob";
    EXPECT_EQ(result.value().begin()->name, "Bob");
}

template <typename ConnType> class WhereLifetimeTest : public StormTestFixture<Person, ConnType> {
  protected:
    auto on_after_setup(const std::shared_ptr<ConnType>&) -> void override {
        ASSERT_TRUE((storm::test::batch_insert<Person, ConnType>(
                std::vector<Person>(storm::test::PEOPLE_25.begin(), storm::test::PEOPLE_25.end())
        )));
    }
};

TYPED_TEST_SUITE(WhereLifetimeTest, DatabaseTypes);

// Test: operand built from a string_view into a heap temporary survives a deferred bind.
TYPED_TEST(WhereLifetimeTest, StringViewOperandSurvivesDeferredBind) {
    // Build the expression from a string_view into a heap temporary, then let the temporary die.
    Expr expr = [] {
        auto             owner = std::make_unique<std::string>("Bob");
        std::string_view view  = *owner;
        return f<^^Person::name>() == view; // node must copy "Bob", not keep a view into *owner
    }();
    // `owner` is freed here; ASAN poisons its buffer.

    expect_single_bob<TypeParam>(expr);
}

// Test: const char* operand from a heap buffer survives a deferred bind (char* alternative).
TYPED_TEST(WhereLifetimeTest, CharPtrOperandSurvivesDeferredBind) {
    Expr expr = [] {
        auto        owner = std::make_unique<std::array<char, 4>>(std::array<char, 4>{'B', 'o', 'b', '\0'});
        const char* ptr   = owner->data();
        return f<^^Person::name>() == ptr; // node must copy "Bob", not keep the char*
    }();

    expect_single_bob<TypeParam>(expr);
}

// Test: collated comparison operand from a temporary string_view also survives a deferred bind.
using SqliteConn = storm::db::sqlite::Connection;

class WhereLifetimeCollateTest : public StormTestFixture<Person, SqliteConn> {
  protected:
    auto on_after_setup(const std::shared_ptr<SqliteConn>&) -> void override {
        ASSERT_TRUE((storm::test::batch_insert<Person, SqliteConn>(
                std::vector<Person>(storm::test::PEOPLE_25.begin(), storm::test::PEOPLE_25.end())
        )));
    }
};

TEST_F(WhereLifetimeCollateTest, CollatedStringViewOperandSurvivesDeferredBind) {
    Expr expr = [] {
        auto             owner = std::make_unique<std::string>("bob");
        std::string_view view  = *owner;
        return f<^^Person::name>().collate(storm::orm::utilities::Collate::NoCase) == view;
    }();

    // Case-insensitive match: "bob" finds "Bob".
    expect_single_bob<SqliteConn>(expr);
}

// NOLINTEND(misc-const-correctness,performance-unnecessary-value-param,performance-unnecessary-copy-initialization)
