#include <gtest/gtest.h>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

import storm_db_sqlite;
import storm_db_postgresql_error;

namespace {

    using SqliteError = storm::db::sqlite::Error;
    using PgError     = storm::db::postgresql::Error;

    // Both Error types must be noexcept-constructible from (int, string_view) so they
    // can be returned inside noexcept catch blocks without re-throwing bad_alloc.
    // This is the core invariant that unblocks bugprone-exception-escape.
    static_assert(std::is_nothrow_constructible_v<SqliteError, int, std::string_view>);
    static_assert(std::is_nothrow_constructible_v<PgError, int, std::string_view>);

    // Trivially copyable so the compiler can memcpy expected<T, Error> through return slots
    // without invoking user-defined operations.
    static_assert(std::is_trivially_copyable_v<SqliteError>);
    static_assert(std::is_trivially_copyable_v<PgError>);

    TEST(ErrorConstruction, SqliteFromLiteral) {
        const SqliteError err{42, "boom"};
        EXPECT_EQ(err.code(), 42);
        EXPECT_EQ(err.message(), std::string_view{"boom"});
    }

    TEST(ErrorConstruction, PostgresFromLiteral) {
        const PgError err{42, "boom"};
        EXPECT_EQ(err.code(), 42);
        EXPECT_EQ(err.message(), std::string_view{"boom"});
    }

    TEST(ErrorConstruction, SqliteFromEmptyMessage) {
        const SqliteError err{0, ""};
        EXPECT_TRUE(err.message().empty());
    }

    TEST(ErrorConstruction, PostgresFromEmptyMessage) {
        const PgError err{0, ""};
        EXPECT_TRUE(err.message().empty());
    }

    TEST(ErrorConstruction, SqliteTruncatesOverlongMessage) {
        const std::string big(1000, 'x');
        const SqliteError err{-1, big};
        EXPECT_LE(err.message().size(), std::size_t{255});
        EXPECT_GT(err.message().size(), std::size_t{0});
        EXPECT_EQ(err.message().front(), 'x');
        EXPECT_EQ(err.message().back(), 'x');
    }

    TEST(ErrorConstruction, PostgresTruncatesOverlongMessage) {
        const std::string big(1000, 'y');
        const PgError     err{-1, big};
        EXPECT_LE(err.message().size(), std::size_t{255});
        EXPECT_GT(err.message().size(), std::size_t{0});
        EXPECT_EQ(err.message().front(), 'y');
        EXPECT_EQ(err.message().back(), 'y');
    }

    TEST(ErrorConstruction, SqliteAcceptsCStringFromBackend) {
        const char*       backend_msg = "no such table";
        const SqliteError err{1, backend_msg};
        EXPECT_EQ(err.message(), std::string_view{"no such table"});
    }

} // namespace
