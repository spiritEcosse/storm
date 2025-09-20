#pragma once

#include <gtest/gtest.h>
#include <storm/storm.h> // Main Storm ORM header (includes macros)

import storm.selectable_query;
import storm.utils;
import <string>;
import <vector>;
import <memory>;
import <set>;
import <sstream>;
import <iomanip>;
import <variant>;
import <ranges>;
import <print>;
import <expected>;
import <map>;
import <iostream>;
import <cmath>;
import <algorithm>;
import <format>;
import <optional>;
import <concepts>;
import <utility>;
import <stdexcept>;
import <flat_map>;

using namespace storm;

// Type aliases for test compatibility
using ValueMap                              = std::flat_map<std::string, SqlValue, std::less<>>;
using ValueVectorMap                        = std::vector<ValueMap>;
using ExpectedValueVectorMap                = std::expected<ValueVectorMap, std::string>;
template <typename T> using ExpectedT       = std::expected<T, std::string>;
template <typename T> using ExpectedVectorT = std::expected<std::vector<T>, std::string>;

// Simple Statement shim for tests to run arbitrary SQL using the unified base.
// We bind it to a model type (Author by default) though DDL doesn't use T.
template <typename T> class TestStatement : public storm::UnifiedStatementBase<TestStatement<T>, T> {
  public:
    using Base = storm::UnifiedStatementBase<TestStatement<T>, T>;
    explicit TestStatement(std::shared_ptr<Connection> c, const std::string& sql) : Base(std::move(c)) {
        if (auto r = this->Base::set_sql(sql); !r) {
            throw std::runtime_error(std::string{"Failed to prepare statement in test: "} + r.error());
        }
    }

    using Base::bind;
    using Base::bind_null;
    using Base::execute;
    using Base::execute_query;
    using Base::get_parameter_index;
    using Base::reset;
    using Base::sql;
};

// Note: alias moved below after Author is defined to ensure complete type

struct Author {
    int         id;
    std::string name;
    int         age;
    std::string email;
    bool        is_active;
    double      rating;
    float       score;
    std::string middleName;
    std::string biography;

    Author() = default;
    Author(const std::string& n,
           int                a,
           const std::string& e,
           int                id         = 0,
           bool               active     = true,
           double             rating     = 0.0,
           float              score      = 0.0,
           const std::string& middleName = "",
           const std::string& biography  = "")
        : id(id)
        , name(n)
        , age(a)
        , email(e)
        , is_active(active)
        , rating(rating)
        , score(score)
        , middleName(middleName)
        , biography(biography) {}
};

// Backwards-compatible alias used in tests (now that Author is defined)
using Statement = TestStatement<Author>;

struct Post {
    int         id = 0;
    std::string title;
    std::string content;
    int         author_id;
    int         views = 0;

    Post() = default;
    Post(const std::string& t, const std::string& c, int author_id, int id = 0, int views = 0)
        : id(id), title(t), content(c), author_id(author_id), views(views) {}
};

// Register reflection metadata for test models using storm.reflect (no macros)
template <> struct ::refl::type_info<Author> {
    static constexpr bool has_reflection = true;
    using descriptor                     = ::refl::meta::type_descriptor<
                                Author,
                                storm::utils::fixed_string{"Author"},
                                ::refl::meta::member_descriptor<&Author::id, storm::utils::fixed_string<16>{"id"}>,
                                ::refl::meta::member_descriptor<&Author::name, storm::utils::fixed_string<16>{"name"}>,
                                ::refl::meta::member_descriptor<&Author::age, storm::utils::fixed_string<16>{"age"}>,
                                ::refl::meta::member_descriptor<&Author::email, storm::utils::fixed_string<16>{"email"}>,
                                ::refl::meta::member_descriptor<&Author::is_active, storm::utils::fixed_string<16>{"is_active"}>,
                                ::refl::meta::member_descriptor<&Author::rating, storm::utils::fixed_string<16>{"rating"}>,
                                ::refl::meta::member_descriptor<&Author::score, storm::utils::fixed_string<16>{"score"}>,
                                ::refl::meta::member_descriptor<&Author::middleName, storm::utils::fixed_string<16>{"middleName"}>,
                                ::refl::meta::member_descriptor<&Author::biography, storm::utils::fixed_string<16>{"biography"}>>;
};

template <> struct ::refl::type_info<Post> {
    static constexpr bool has_reflection = true;
    using descriptor                     = ::refl::meta::type_descriptor<
                                Post,
                                storm::utils::fixed_string<16>{"Post"},
                                ::refl::meta::member_descriptor<&Post::id, storm::utils::fixed_string<16>{"id"}>,
                                ::refl::meta::member_descriptor<&Post::title, storm::utils::fixed_string<16>{"title"}>,
                                ::refl::meta::member_descriptor<&Post::content, storm::utils::fixed_string<16>{"content"}>,
                                ::refl::meta::member_descriptor<&Post::author_id, storm::utils::fixed_string<16>{"author_id"}>,
                                ::refl::meta::member_descriptor<&Post::views, storm::utils::fixed_string<16>{"views"}>>;
};

class ORMTest : public ::testing::Test {
  protected:
    std::shared_ptr<Connection> conn;
    std::string                 db_name;
    int                         alice_id;
    int                         bob_id;
    int                         charlie_id;
    int                         diana_id;

    void SetUp() override {
        // Use in-memory SQLite database for isolation
        db_name = ":memory:";
        conn    = std::make_shared<Connection>(db_name);

        // Create test tables
        std::string create_author_sql = R"(
            CREATE TABLE IF NOT EXISTS author (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                age INTEGER,
                email TEXT,
                is_active BOOLEAN,
                rating REAL,
                score REAL,
                middleName TEXT,
                biography TEXT
            );
        )";
        auto        author_stmt       = Statement(conn, create_author_sql);
        ASSERT_TRUE(author_stmt.execute()) << "Failed to create author table";

        std::string create_post_sql = R"(
            CREATE TABLE IF NOT EXISTS post (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                title TEXT NOT NULL,
                content TEXT,
                author_id INTEGER,
                views INTEGER,
                FOREIGN KEY (author_id) REFERENCES author(id)
            );
        )";
        auto        post_stmt       = Statement(conn, create_post_sql);
        ASSERT_TRUE(post_stmt.execute()) << "Failed to create post table";

        // Setup test data
        setupTestData();
    }

    void setupTestData() {
        // Create authors with essential test data
        std::vector<Author> authors =
                {{"Alice Smith", 25, "alice@example.com", 0, true, 4.5, 85.5, "Alice", "Alice biography"},
                 {"Bob Johnson", 35, "bob@example.com", 0, true, 5.0, 90.0, "Bob", "Bob biography"},
                 {"Charlie Brown", 30, "charlie@example.com", 0, true, 4.0, 80.0, "Charlie", "Charlie biography"},
                 {"Diana Prince", 28, "diana@example.com", 0, true, 5.5, 95.0, "Diana", "Diana biography"}};

        auto author_ids = QuerySet<Author>(conn).insert(authors).value();
        alice_id        = author_ids[0];
        bob_id          = author_ids[1];
        charlie_id      = author_ids[2];
        diana_id        = author_ids[3];

        // Create posts with structured data: {author_id, post_count, base_views}
        std::vector<Post> posts;
        struct {
            int author_id;
            int count;
            int base_views;
        } postData[] = {{alice_id, 3, 100}, {bob_id, 2, 250}, {charlie_id, 4, 120}, {diana_id, 2, 350}};

        for (const auto& [author_id, count, base_views] : postData) {
            std::string author_name = (author_id == alice_id)     ? "Alice"
                                      : (author_id == bob_id)     ? "Bob"
                                      : (author_id == charlie_id) ? "Charlie"
                                                                  : "Diana";
            for (int i = 1; i <= count; ++i) {
                std::string ordinal = (i == 1) ? "First" : (i == 2) ? "Second" : (i == 3) ? "Third" : "Fourth";
                posts.emplace_back(
                        author_name + "'s " + ordinal + " Post",
                        "Content " + author_name.substr(0, 1) + std::to_string(i),
                        author_id,
                        0,
                        base_views + (i - 1) * 50
                );
            }
        }
        QuerySet<Post>(conn).insert(posts);
    }

    void TearDown() override {
        conn.reset();
    }

    void clearTestData() {
        // Clear all data from tables
        QuerySet<Post>(conn).remove();
        QuerySet<Author>(conn).remove();
    }
};

// Constant for floating-point comparison epsilon
constexpr double FLOAT_COMPARISON_EPSILON = 0.001;

// Treat multiple representations as null for SqlValue
static inline bool IsNullValue(const storm::SqlValue& v) {
    return std::holds_alternative<std::monostate>(v) || std::holds_alternative<std::nullopt_t>(v) ||
           std::holds_alternative<std::nullptr_t>(v);
}

// If variant holds an arithmetic (non-bool) type, return it as double
static inline std::optional<double> ToNumeric(const storm::SqlValue& v) {
    return std::visit(
            [](const auto& val) -> std::optional<double> {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, bool>) {
                    return std::nullopt; // treat bool separately
                } else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
                    return static_cast<double>(val);
                } else {
                    return std::nullopt;
                }
            },
            v
    );
}

bool VariantsEqual(const storm::SqlValue& lhs, const storm::SqlValue& rhs) {
    // Null semantics
    const bool lhs_null = IsNullValue(lhs);
    const bool rhs_null = IsNullValue(rhs);
    if (lhs_null || rhs_null)
        return lhs_null && rhs_null;

    // Numeric semantics (covers all integral/fp alternatives except bool)
    auto lnum = ToNumeric(lhs);
    auto rnum = ToNumeric(rhs);
    if (lnum && rnum) {
        return std::abs(*lnum - *rnum) < FLOAT_COMPARISON_EPSILON;
    }

    // Bool semantics
    if (std::holds_alternative<bool>(lhs) && std::holds_alternative<bool>(rhs)) {
        return std::get<bool>(lhs) == std::get<bool>(rhs);
    }

    // String semantics
    if (std::holds_alternative<std::string>(lhs) && std::holds_alternative<std::string>(rhs)) {
        return std::get<std::string>(lhs) == std::get<std::string>(rhs);
    }

    // Fallback to strict same-type comparison
    return std::visit(
            [](const auto& l, const auto& r) -> bool {
                using LType = std::decay_t<decltype(l)>;
                using RType = std::decay_t<decltype(r)>;
                if constexpr (std::is_same_v<LType, RType>) {
                    if constexpr (std::is_same_v<LType, std::nullopt_t>) {
                        return true; // Two nullopt_t are always equal
                    } else {
                        return l == r;
                    }
                } else {
                    return false;
                }
            },
            lhs,
            rhs
    );
}

// Helper function to convert storm::SqlValue to string for error messages
std::string SqlValueToString(const storm::SqlValue& var) {
    if (IsNullValue(var))
        return "null";

    return std::visit(
            [](const auto& val) -> std::string {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    return std::format("\"{}\"", val);
                } else if constexpr (std::is_same_v<T, bool>) {
                    return std::format("{}", val);
                } else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
                    return std::format("{}", val);
                } else {
                    // Should not reach for other alternatives as they were handled above
                    return std::string{"null"};
                }
            },
            var
    );
}

// Helper function to verify an entire vector of result maps
void AssertResultsMatch(const ExpectedValueVectorMap& actual_result, const ValueVectorMap& expected_results) {
    ASSERT_TRUE(actual_result.has_value()) << "failed with error: " << actual_result.error();
    auto actual_results = actual_result.value();

    ASSERT_EQ(actual_results.size(), expected_results.size()) << std::format(
            "Result count mismatch. Expected {} results, got {}", expected_results.size(), actual_results.size()
    );

    // Use bitset for tracking matched results
    std::vector<bool> matched(actual_results.size());

    // For each expected result, find a matching actual result
    for (size_t expected_idx = 0; const auto& expected : expected_results) {
        ASSERT_FALSE(expected.empty()) << std::format("Expected result map {} is empty", expected_idx);

        auto find_match = [&](const auto& actual_with_idx) -> bool {
            auto&& [actual_idx, actual] = actual_with_idx;

            if (matched[actual_idx])
                return false;

            // Check size first for early exit
            if (actual.size() != expected.size())
                return false;

            // Use ranges algorithms for key-value matching
            return std::ranges::all_of(expected, [&](const auto& expected_pair) {
                auto&& [key, expected_val] = expected_pair;
                return actual.contains(key) && VariantsEqual(actual.at(key), expected_val);
            });
        };

        // Find matching result using traditional approach
        bool found_match = false;
        for (size_t actual_idx = 0; const auto& actual : actual_results) {
            if (find_match(std::make_pair(actual_idx, actual))) {
                matched[actual_idx] = true;
                found_match         = true;
                break;
            }
            ++actual_idx;
        }

        if (!found_match) {
            // Build comprehensive error message using ranges
            auto build_map_string = [](const auto& map) -> std::string {
                std::string result = "{";
                bool        first  = true;
                for (const auto& [key, val] : map) {
                    if (!first)
                        result += ", ";
                    result += std::format("{}: {}", key, SqlValueToString(val));
                    first = false;
                }
                result += "}";
                return result;
            };

            auto error_msg = std::format(
                    "No matching result found for expected result {}. Expected: {}\n"
                    "Actual results:\n",
                    expected_idx,
                    build_map_string(expected)
            );

            for (size_t i = 0; const auto& result : actual_results) {
                error_msg += std::format("  [{}] {}\n", i, build_map_string(result));
                ++i;
            }

            ASSERT_TRUE(false) << error_msg;
        }
        ++expected_idx;
    }
}

// =======================================
// INSERT TESTS
// =======================================
TEST_F(ORMTest, InsertOperations) {
    // Test single object insert
    Author single("John Doe", 30, "john@example.com");
    auto   singleResult = QuerySet<Author>(conn).insert(single);
    EXPECT_TRUE(singleResult.has_value()) << singleResult.error();
    EXPECT_GT(singleResult.value(), 0);
    EXPECT_EQ(single.id, 0); // Original object unchanged

    // Test multiple object insert
    std::vector<Author> multiple =
            {{"Eve Adams", 28, "eve@example.com"},
             {"Frank Miller", 32, "frank@example.com"},
             {"Grace Wilson", 26, "grace@example.com"}};
    auto multipleResult = QuerySet<Author>(conn).insert(multiple);
    EXPECT_TRUE(multipleResult.has_value()) << multipleResult.error();
    EXPECT_EQ(multipleResult.value().size(), multiple.size());
    for (const auto& id : multipleResult.value()) {
        EXPECT_GT(id, 0);
    }

    // Test empty vector insert
    std::vector<Author> empty;
    auto                emptyResult = QuerySet<Author>(conn).insert(empty);
    EXPECT_TRUE(emptyResult.has_value()) << emptyResult.error();

    // Test edge case with unusual data
    Author edgeCase("", -1, "");
    auto   edgeResult = QuerySet<Author>(conn).insert(edgeCase);
    EXPECT_TRUE(edgeResult.has_value()) << edgeResult.error();
}

// =======================================
// UPDATE TESTS
// =======================================
TEST_F(ORMTest, UpdateOperations) {
    // Test single object update
    Author single("John Doe", 30, "john@example.com");
    auto   insertedId = QuerySet<Author>(conn).insert(single);
    ASSERT_TRUE(insertedId.has_value()) << insertedId.error();

    single.id         = insertedId.value();
    single.name       = "John Smith";
    single.age        = 31;
    auto singleResult = QuerySet<Author>(conn).update(single);
    EXPECT_TRUE(singleResult.has_value()) << singleResult.error();
    EXPECT_TRUE(singleResult.value());

    // Test multiple object update
    std::vector<Author> multiple    = {{"Eve Adams", 28, "eve@example.com"}, {"Frank Miller", 32, "frank@example.com"}};
    auto                multipleIds = QuerySet<Author>(conn).insert(multiple);
    ASSERT_TRUE(multipleIds.has_value()) << multipleIds.error();

    for (size_t i = 0; i < multiple.size(); ++i) {
        multiple[i].id = multipleIds.value()[i];
    }
    multiple[0].age     = 29;
    multiple[1].name    = "Robert";
    auto multipleResult = QuerySet<Author>(conn).update(multiple);
    EXPECT_TRUE(multipleResult.has_value()) << multipleResult.error();
    EXPECT_TRUE(multipleResult.value());

    // Test empty vector and non-existent object
    std::vector<Author> empty;
    auto                emptyResult = QuerySet<Author>(conn).update(empty);
    EXPECT_TRUE(emptyResult.has_value()) << emptyResult.error();

    Author nonExistent("Ghost", 0, "ghost@example.com");
    nonExistent.id   = 99999;
    auto ghostResult = QuerySet<Author>(conn).update(nonExistent);
    EXPECT_TRUE(ghostResult.has_value()) << ghostResult.error();
}

// =======================================
// REMOVE TESTS
// =======================================
TEST_F(ORMTest, RemoveOperations) {
    // Test single object remove
    Author single("John Doe", 30, "john@example.com");
    auto   insertedId = QuerySet<Author>(conn).insert(single);
    ASSERT_TRUE(insertedId.has_value()) << insertedId.error();

    single.id         = insertedId.value();
    auto singleResult = QuerySet<Author>(conn).remove(single);
    EXPECT_TRUE(singleResult.has_value()) << singleResult.error();
    EXPECT_TRUE(singleResult.value());

    // Test multiple object remove
    std::vector<Author> multiple    = {{"Eve Adams", 28, "eve@example.com"}, {"Frank Miller", 32, "frank@example.com"}};
    auto                multipleIds = QuerySet<Author>(conn).insert(multiple);
    ASSERT_TRUE(multipleIds.has_value()) << multipleIds.error();

    for (size_t i = 0; i < multiple.size(); ++i) {
        multiple[i].id = multipleIds.value()[i];
    }
    auto multipleResult = QuerySet<Author>(conn).remove(multiple);
    EXPECT_TRUE(multipleResult.has_value()) << multipleResult.error();
    EXPECT_TRUE(multipleResult.value());

    // Test edge cases: empty vector and non-existent object
    std::vector<Author> empty;
    auto                emptyResult = QuerySet<Author>(conn).remove(empty);
    EXPECT_FALSE(emptyResult.has_value());
    EXPECT_EQ(emptyResult.error(), "Empty objects vector");

    Author nonExistent("Ghost", 0, "ghost@example.com");
    nonExistent.id   = 99999;
    auto ghostResult = QuerySet<Author>(conn).remove(nonExistent);
    EXPECT_TRUE(ghostResult.has_value()) << ghostResult.error();
}

TEST_F(ORMTest, RemoveAllAndByCondition) {
    // Test remove all
    auto initialCount = QuerySet<Author>(conn).select_all();
    EXPECT_TRUE(initialCount.has_value()) << initialCount.error();
    ASSERT_GT(initialCount.value().size(), 0);

    auto removeAllResult = QuerySet<Author>(conn).remove();
    EXPECT_TRUE(removeAllResult.has_value()) << removeAllResult.error();

    auto afterRemoveAll = QuerySet<Author>(conn).select_all();
    EXPECT_TRUE(afterRemoveAll.has_value()) << afterRemoveAll.error();
    EXPECT_EQ(afterRemoveAll.value().size(), 0);

    // Reset data and test conditional remove
    setupTestData();
    auto newInitialCount = QuerySet<Author>(conn).select_all();
    auto olderAuthors    = QuerySet<Author>(conn).where(field(&Author::age) > 30).select_all();
    ASSERT_TRUE(newInitialCount.has_value() && olderAuthors.has_value());
    ASSERT_GT(olderAuthors.value().size(), 0);

    auto conditionalRemove = QuerySet<Author>(conn).where(field(&Author::age) > 30).remove();
    EXPECT_TRUE(conditionalRemove.has_value()) << conditionalRemove.error();

    auto remaining = QuerySet<Author>(conn).select_all();
    EXPECT_TRUE(remaining.has_value()) << remaining.error();
    EXPECT_EQ(remaining.value().size(), newInitialCount.value().size() - olderAuthors.value().size());

    for (const auto& author : remaining.value()) {
        EXPECT_LE(author.age, 30);
    }
}

// =======================================
// QUERYSET COPY TESTS
// =======================================
TEST_F(ORMTest, QuerySetCopyAndAssignment) {
    // Test 1: Basic copy construction and modification independence
    auto baseQuery   = QuerySet<Author>(conn).where(field(&Author::is_active) == true);
    auto youngQuery  = baseQuery; // Copy NOSONAR
    auto seniorQuery = baseQuery; // Copy NOSONAR

    youngQuery.where(field(&Author::age) <= 30);
    seniorQuery.where(field(&Author::age) > 30);

    auto youngResults  = youngQuery.select_all();
    auto seniorResults = seniorQuery.select_all();
    auto baseResults   = baseQuery.select_all();

    ASSERT_TRUE(youngResults.has_value()) << youngResults.error();
    ASSERT_TRUE(seniorResults.has_value()) << seniorResults.error();
    ASSERT_TRUE(baseResults.has_value()) << baseResults.error();

    // Verify filters work and original is unchanged
    for (const auto& author : youngResults.value()) {
        EXPECT_TRUE(author.is_active);
        EXPECT_LE(author.age, 30);
    }
    for (const auto& author : seniorResults.value()) {
        EXPECT_TRUE(author.is_active);
        EXPECT_GT(author.age, 30);
    }
    EXPECT_EQ(youngResults.value().size() + seniorResults.value().size(), baseResults.value().size());

    // Test 2: Copy assignment operator
    auto query1 = QuerySet<Author>(conn).where(field(&Author::age) < 30).order_by(field(&Author::name), true);
    auto query2 = QuerySet<Author>(conn).where(field(&Author::rating) > 4.0).limit(2);

    auto originalQuery1Results = query1.select_all();
    query2                     = query1; // Copy assignment
    auto newQuery2Results      = query2.select_all();

    ASSERT_TRUE(originalQuery1Results.has_value()) << originalQuery1Results.error();
    ASSERT_TRUE(newQuery2Results.has_value()) << newQuery2Results.error();
    EXPECT_EQ(originalQuery1Results.value().size(), newQuery2Results.value().size());

    // Self-assignment should be safe
    query1               = query1;
    auto afterSelfAssign = query1.select_all();
    ASSERT_TRUE(afterSelfAssign.has_value()) << afterSelfAssign.error();
    EXPECT_EQ(originalQuery1Results.value().size(), afterSelfAssign.value().size());
}

TEST_F(ORMTest, QuerySetCopyComprehensive) {
    // Test all QuerySet member variables in a single comprehensive test
    auto complexQuery = QuerySet<Author>(conn)
                                .where(field(&Author::is_active) == true)
                                .where(field(&Author::age) > 20)
                                .order_by(field(&Author::age), false)
                                .order_by(field(&Author::name), true)
                                .distinct(field(&Author::age))
                                .only(field(&Author::name), field(&Author::age))
                                .group_by(field(&Author::is_active))
                                .limit(10)
                                .offset(1);

    auto copiedQuery = complexQuery; // Copy NOSONAR

    // Both should produce identical results initially
    auto originalResults = complexQuery.select_all();
    auto copiedResults   = copiedQuery.select_all();
    ASSERT_TRUE(originalResults.has_value()) << originalResults.error();
    ASSERT_TRUE(copiedResults.has_value()) << copiedResults.error();
    EXPECT_EQ(originalResults.value().size(), copiedResults.value().size());

    // Test member independence: modify copy without affecting original
    copiedQuery.where(field(&Author::rating) > 4.0);
    auto newOriginalResults = complexQuery.select_all();
    auto newCopiedResults   = copiedQuery.select_all();

    ASSERT_TRUE(newOriginalResults.has_value()) << newOriginalResults.error();
    ASSERT_TRUE(newCopiedResults.has_value()) << newCopiedResults.error();
    EXPECT_EQ(originalResults.value().size(), newOriginalResults.value().size());
    EXPECT_LE(newCopiedResults.value().size(), originalResults.value().size());

    // Test edge cases: empty query, no results, consecutive copies
    auto emptyQuery       = QuerySet<Author>(conn);
    auto emptyQueryCopy   = emptyQuery;
    auto emptyResults     = emptyQuery.select_all();
    auto emptyCopyResults = emptyQueryCopy.select_all();
    ASSERT_TRUE(emptyResults.has_value()) << emptyResults.error();
    ASSERT_TRUE(emptyCopyResults.has_value()) << emptyCopyResults.error();
    EXPECT_EQ(emptyResults.value().size(), emptyCopyResults.value().size());

    // Test consecutive copies
    auto activeQuery = QuerySet<Author>(conn).where(field(&Author::is_active) == true);
    auto copy1       = activeQuery;
    auto copy2       = copy1;
    auto copy3       = copy2;

    copy1.where(field(&Author::age) < 30);
    copy2.where(field(&Author::rating) > 4.0);
    copy3.where(field(&Author::name).like("A%"));

    auto baseRes  = activeQuery.select_all();
    auto copy1Res = copy1.select_all();
    auto copy2Res = copy2.select_all();
    auto copy3Res = copy3.select_all();

    ASSERT_TRUE(baseRes.has_value() && copy1Res.has_value() && copy2Res.has_value() && copy3Res.has_value());

    // Verify each copy has independent filters
    for (const auto& author : copy1Res.value())
        EXPECT_LT(author.age, 30);
    for (const auto& author : copy2Res.value())
        EXPECT_GT(author.rating, 4.0);
    for (const auto& author : copy3Res.value())
        EXPECT_EQ('A', author.name[0]);
}

// =======================================
// INTEGRATION TESTS
// =======================================
TEST_F(ORMTest, FullCRUDWorkflow) {
    // Create
    Author author("John Doe", 30, "john@example.com");
    auto   result = QuerySet<Author>(conn).insert(author);
    ASSERT_TRUE(result.has_value()) << "Insert should return a value" << result.error();
    int id = result.value();
    ASSERT_GT(id, 0);

    // Update
    author.id   = id;
    author.name = "John Smith";
    author.age  = 31;
    ASSERT_TRUE(QuerySet<Author>(conn).update(author));
    EXPECT_EQ(author.id, id) << "ID should remain unchanged after update";

    // Delete
    ASSERT_TRUE(QuerySet<Author>(conn).remove(author));
}

TEST_F(ORMTest, BatchOperationsWorkflow) {
    // Batch insert
    std::vector<Author> authors =
            {Author("Eve Adams", 28, "eve@example.com"),
             Author("Frank Miller", 32, "frank@example.com"),
             Author("Grace Wilson", 26, "grace@example.com")};
    auto result = QuerySet<Author>(conn).insert(authors);
    ASSERT_TRUE(result.has_value()) << "Insert should return a value" << result.error();
    ASSERT_EQ(result.value().size(), authors.size());

    // Verify all returned IDs are valid
    for (const auto& id : result.value()) {
        ASSERT_GT(id, 0);
    }

    // Set IDs and batch update
    for (size_t i = 0; i < authors.size(); ++i) {
        authors[i].id = result.value()[i];
        authors[i].age += 1;
    }
    ASSERT_TRUE(QuerySet<Author>(conn).update(authors));

    // Batch delete
    ASSERT_TRUE(QuerySet<Author>(conn).remove(authors));
}

TEST_F(ORMTest, MixedOperations) {
    // Insert single
    Author single_author("Single", 25, "single@example.com");
    auto   result = QuerySet<Author>(conn).insert(single_author);
    ASSERT_TRUE(result.has_value()) << "Insert should return a value" << result.error();
    ASSERT_GT(result.value(), 0);

    // Insert batch
    std::vector<Author> batch_authors =
            {Author("Batch1", 30, "batch1@example.com"), Author("Batch2", 35, "batch2@example.com")};
    auto result2 = QuerySet<Author>(conn).insert(batch_authors);
    ASSERT_TRUE(result2.has_value()) << "Insert should return a value" << result2.error();
    auto result2Value = result2.value();
    ASSERT_EQ(result2Value.size(), batch_authors.size());

    // Update single
    single_author.id  = result.value();
    single_author.age = 26;
    result            = QuerySet<Author>(conn).update(single_author);
    ASSERT_TRUE(result.has_value()) << "Update should return a value" << result.error();
    ASSERT_EQ(result.value(), true);

    // Update batch
    for (size_t i = 0; i < batch_authors.size(); ++i) {
        batch_authors[i].id = result2Value[i];
        batch_authors[i].age += 1;
    }
    result = QuerySet<Author>(conn).update(batch_authors);
    ASSERT_TRUE(result.has_value()) << "Update should return a value" << result.error();
    ASSERT_EQ(result.value(), true);

    // Remove single
    result = QuerySet<Author>(conn).remove(single_author);
    ASSERT_TRUE(result.has_value()) << "Remove should return a value" << result.error();
    ASSERT_EQ(result.value(), true);

    // Remove batch
    result = QuerySet<Author>(conn).remove(batch_authors);
    ASSERT_TRUE(result.has_value()) << "Remove should return a value" << result.error();
    ASSERT_EQ(result.value(), true);
}

// =======================================
// SELECT TESTS
// =======================================
TEST_F(ORMTest, SelectAll) {
    auto all_authors = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(all_authors.has_value()) << "Select should return a value" << all_authors.error();
    ASSERT_EQ(all_authors.value().size(), 4); // Alice, Bob, Charlie, Diana
}

TEST_F(ORMTest, SelectAllWhereId) {
    auto authors = QuerySet<Author>(conn).where(field(&Author::id) == alice_id).select_all();
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto value = authors.value();
    EXPECT_EQ(value[0].name, "Alice Smith");
}

TEST_F(ORMTest, SelectAllWhereMany) {
    auto posts = QuerySet<Post>(conn)
                         .where(field(&Post::author_id) == alice_id)
                         .where(field(&Post::title) == "Alice's First Post")
                         .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 1);
    auto value = posts.value();
    EXPECT_EQ(value[0].title, "Alice's First Post");
}

TEST_F(ORMTest, SelectAllErrorInvalidColumnException) {
    try {
        QuerySet<Post>(conn).where(field(&Author::name) == "Alice Smith").select_all();
        FAIL() << "Expected InvalidColumnException to be thrown";
    } catch (const InvalidColumnException& e) {
        // Verify the column name in the exception
        EXPECT_EQ(e.getColumnName(), "author.name");
        EXPECT_FALSE(e.getQuery().empty());
    } catch (const std::exception& e) {
        FAIL() << "Expected InvalidColumnException, but a different exception was "
                  "thrown: "
               << e.what();
    }
}

// =======================================
// JOIN TESTS
// =======================================
TEST_F(ORMTest, SelectAllWithJoin) {
    auto all_posts = QuerySet<Post>(conn).join(field(&Post::author_id)).select_all();
    ASSERT_TRUE(all_posts.has_value()) << "Select should return a value" << all_posts.error();
    ASSERT_EQ(all_posts.value().size(),
              11); // Should have all 11 posts with author data
}

TEST_F(ORMTest, SelectAllWithJoinWhere) {
    auto posts =
            QuerySet<Post>(conn).join(field(&Post::author_id)).where(field(&Post::author_id) == alice_id).select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 3);
    auto value = posts.value();
    EXPECT_EQ(value[0].title, "Alice's First Post");
}

TEST_F(ORMTest, SelectAllWithJoinWhereMany) {
    auto posts = QuerySet<Post>(conn)
                         .join(field(&Post::author_id))
                         .where(field(&Post::author_id) == alice_id)
                         .where(field(&Author::is_active) == true)
                         .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 3);
    auto value = posts.value();
    EXPECT_EQ(value[0].title, "Alice's First Post");
}

TEST_F(ORMTest, SelectAllWithJoinWhereLimit) {
    auto posts = QuerySet<Post>(conn)
                         .join(field(&Post::author_id))
                         .where(field(&Post::author_id) == alice_id)
                         .limit(1)
                         .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 1);
    EXPECT_EQ(posts.value()[0].title, "Alice's First Post");
}

TEST_F(ORMTest, SelectAllWithJoinWhereOffset) {
    auto posts = QuerySet<Post>(conn)
                         .join(field(&Post::author_id))
                         .where(field(&Post::author_id) == alice_id)
                         .offset(1)
                         .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(),
              2); // After offset(1), we should have 2 post remaining
}

TEST_F(ORMTest, SelectAllWithJoinWhereLimitOffset) {
    auto posts = QuerySet<Post>(conn)
                         .join(field(&Post::author_id))
                         .where(field(&Post::author_id) == alice_id)
                         .limit(1)
                         .offset(1)
                         .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(),
              1); // Only one post for Alice, offset 1 means no results
}

TEST_F(ORMTest, SelectAllWithJoinWhereGroupBy) {
    auto posts = QuerySet<Post>(conn)
                         .join(field(&Post::author_id))
                         .where(field(&Post::author_id) == alice_id)
                         .group_by(field(&Author::name))
                         .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 1);
}

TEST_F(ORMTest, SelectAllWithJoinWhereGroupByLimit) {
    auto posts = QuerySet<Post>(conn)
                         .join(field(&Post::author_id))
                         .where(field(&Post::author_id) == alice_id)
                         .group_by(field(&Author::name))
                         .limit(1)
                         .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 1);
}

TEST_F(ORMTest, SelectAllWithJoinAndWhereAndGroupByAndOffset) {
    auto posts = QuerySet<Post>(conn)
                         .join(field(&Post::author_id))
                         .where(field(&Post::author_id) == alice_id)
                         .group_by(field(&Author::name))
                         .offset(1)
                         .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(),
              0); // Only one group for Alice, offset 1 means no results
}

TEST_F(ORMTest, SelectAllWithJoinWhereGroupByLimitOffset) {
    auto posts = QuerySet<Post>(conn)
                         .join(field(&Post::author_id))
                         .where(field(&Post::author_id) == alice_id)
                         .group_by(field(&Author::name))
                         .limit(1)
                         .offset(1)
                         .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 0);
}

// =======================================
// ORDER BY TESTS
// =======================================
TEST_F(ORMTest, SelectAllWithJoinWhereLimitOffsetOrderBy) {
    auto posts = QuerySet<Post>(conn)
                         .join(field(&Post::author_id))
                         .where(field(&Post::author_id) == alice_id)
                         .limit(1)
                         .offset(1)
                         .order_by(field(&Author::name))
                         .select_all();
    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 1);
    // Check that we got the second post (offset 1) for Alice
    EXPECT_EQ(posts.value()[0].title, "Alice's Second Post");
}

TEST_F(ORMTest, OrderBySingleFieldAscendingExplicit) {
    auto authors = QuerySet<Author>(conn).order_by(field(&Author::name), true).select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto value = authors.value();
    // Check ascending order by name: Alice, Bob, Charlie, Diana
    EXPECT_EQ(value[0].name, "Alice Smith");
    EXPECT_EQ(value[1].name, "Bob Johnson");
    EXPECT_EQ(value[2].name, "Charlie Brown");
    EXPECT_EQ(value[3].name, "Diana Prince");
}

TEST_F(ORMTest, OrderBySingleFieldDescending) {
    auto authors = QuerySet<Author>(conn).order_by(field(&Author::name), false).select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto value = authors.value();

    // Check descending order by name: Diana, Charlie, Bob, Alice
    EXPECT_EQ(value[0].name, "Diana Prince");
    EXPECT_EQ(value[1].name, "Charlie Brown");
    EXPECT_EQ(value[2].name, "Bob Johnson");
    EXPECT_EQ(value[3].name, "Alice Smith");
}

TEST_F(ORMTest, OrderBySingleFieldDefaultAscending) {
    auto authors = QuerySet<Author>(conn)
                           .order_by(field(&Author::age)) // Default is ascending
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto value = authors.value();

    // Check ascending order by age: 25, 28, 30, 35
    EXPECT_EQ(value[0].age, 25); // Alice
    EXPECT_EQ(value[1].age, 28); // Diana
    EXPECT_EQ(value[2].age, 30); // Charlie
    EXPECT_EQ(value[3].age, 35); // Bob

    EXPECT_EQ(value[0].name, "Alice Smith");
    EXPECT_EQ(value[1].name, "Diana Prince");
    EXPECT_EQ(value[2].name, "Charlie Brown");
    EXPECT_EQ(value[3].name, "Bob Johnson");
}

TEST_F(ORMTest, OrderByMultipleFieldsMixedDirections) {
    auto authors = QuerySet<Author>(conn)
                           .order_by(field(&Author::age), true, field(&Author::name), false) // Age ASC, Name DESC
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto authorsValue = authors.value();

    EXPECT_EQ(authorsValue[0].age, 25);
    EXPECT_EQ(authorsValue[0].name, "Alice Smith");

    EXPECT_EQ(authorsValue[1].age, 28);
    EXPECT_EQ(authorsValue[1].name, "Diana Prince");

    EXPECT_EQ(authorsValue[2].age, 30);
    EXPECT_EQ(authorsValue[2].name, "Charlie Brown");

    EXPECT_EQ(authorsValue[3].age, 35);
    EXPECT_EQ(authorsValue[3].name, "Bob Johnson");
}

TEST_F(ORMTest, OrderByWithJoin) {
    auto posts = QuerySet<Post>(conn).join(field(&Post::author_id)).order_by(field(&Author::name), true).select_all();

    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 11);
    auto postsValue = posts.value();

    // Posts should be ordered by their author's name
    EXPECT_EQ(postsValue[0].title, "Alice's First Post");
    EXPECT_EQ(postsValue[1].title, "Alice's Second Post");
    EXPECT_EQ(postsValue[2].title, "Alice's Third Post");
    EXPECT_EQ(postsValue[3].title, "Bob's First Post");
    EXPECT_EQ(postsValue[4].title, "Bob's Second Post");
    EXPECT_EQ(postsValue[5].title, "Charlie's First Post");
    EXPECT_EQ(postsValue[6].title, "Charlie's Second Post");
    EXPECT_EQ(postsValue[7].title, "Charlie's Third Post");
    EXPECT_EQ(postsValue[8].title, "Charlie's Fourth Post");
    EXPECT_EQ(postsValue[9].title, "Diana's First Post");
    EXPECT_EQ(postsValue[10].title, "Diana's Second Post");
}

TEST_F(ORMTest, OrderByAgeDescending) {
    auto authors = QuerySet<Author>(conn).order_by(field(&Author::age), false).select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto authorsValue = authors.value();

    // Should be ordered by age DESC
    EXPECT_EQ(authorsValue[0].age, 35); // Bob (highest age)
    EXPECT_EQ(authorsValue[0].name, "Bob Johnson");

    EXPECT_EQ(authorsValue[1].age, 30); // Charlie
    EXPECT_EQ(authorsValue[1].name, "Charlie Brown");

    EXPECT_EQ(authorsValue[2].age, 28); // Diana
    EXPECT_EQ(authorsValue[2].name, "Diana Prince");

    EXPECT_EQ(authorsValue[3].age, 25); // Alice (lowest age)
    EXPECT_EQ(authorsValue[3].name, "Alice Smith");
}

TEST_F(ORMTest, OrderByComplexJoinWithMultipleOrderFields) {
    auto posts = QuerySet<Post>(conn)
                         .join(field(&Post::author_id))
                         .order_by(field(&Author::name), true)
                         .order_by(field(&Author::age), false)
                         .select_all();

    ASSERT_TRUE(posts.has_value()) << "Select should return a value" << posts.error();
    ASSERT_EQ(posts.value().size(), 11);

    auto postsValue = posts.value();

    // Posts should be ordered by author name ASC, then age DESC
    EXPECT_EQ(postsValue[0].title, "Alice's First Post");
    EXPECT_EQ(postsValue[1].title, "Alice's Second Post");
    EXPECT_EQ(postsValue[2].title, "Alice's Third Post");
    EXPECT_EQ(postsValue[3].title, "Bob's First Post");
    EXPECT_EQ(postsValue[4].title, "Bob's Second Post");
    EXPECT_EQ(postsValue[5].title, "Charlie's First Post");
    EXPECT_EQ(postsValue[6].title, "Charlie's Second Post");
    EXPECT_EQ(postsValue[7].title, "Charlie's Third Post");
    EXPECT_EQ(postsValue[8].title, "Charlie's Fourth Post");
    EXPECT_EQ(postsValue[9].title, "Diana's First Post");
    EXPECT_EQ(postsValue[10].title, "Diana's Second Post");
}

TEST_F(ORMTest, OrderingWithWhereClause) {
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::age) == 30) // Only Charlie has age 30
                           .order_by(field(&Author::name), true)
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "Charlie Brown");
}

// =======================================
// WHERE TESTS
// =======================================
TEST_F(ORMTest, WhereClauseEqualsDefault) {
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::age) == 30) // Only Charlie has age 30
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "Charlie Brown");
}

TEST_F(ORMTest, WhereClauseEqualsOperator) {
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::age) == 30) // Only Charlie has age 30
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "Charlie Brown");
}

TEST_F(ORMTest, WhereClauseConvenientSyntax) {
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::age) == 30) // Default to EQUALS
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "Charlie Brown");
}

TEST_F(ORMTest, WhereClauseMultipleConditions) {
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::age) > 25)
                           .where(field(&Author::is_active) == true)
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_GT(author.age, 25);
        EXPECT_TRUE(author.is_active);
    }
}

TEST_F(ORMTest, WhereClauseNTTPSyntax) {
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::age) == 30) // NTTP syntax - member pointer as template parameter
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "Charlie Brown");
}

TEST_F(ORMTest, WhereClauseNTTPStringComparison) {
    auto authors = QuerySet<Author>(conn).where(field(&Author::name) == "Alice Smith").select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "Alice Smith");
    EXPECT_EQ(authorsValue[0].age, 25);
}

TEST_F(ORMTest, WhereClauseNTTPBooleanValue) {
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::is_active) == true) // Boolean using NTTP
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_TRUE(author.is_active);
    }
}

TEST_F(ORMTest, WhereClauseNTTPWithOperators) {
    // Test NTTP with GREATER_THAN operator
    auto authors = QuerySet<Author>(conn).where(field(&Author::age) > 25).select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_GT(author.age, 25);
    }
}

TEST_F(ORMTest, WhereClauseNTTPLikeOperator) {
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::name).like("A%")) // Names starting with 'A'
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_EQ(author.name[0], 'A');
    }
}

TEST_F(ORMTest, WhereClauseNTTPChaining) {
    // Test chaining NTTP where clauses
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::age) >= 20)
                           .where(field(&Author::is_active) == true)
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_GE(author.age, 20);
        EXPECT_TRUE(author.is_active);
    }
}

TEST_F(ORMTest, WhereClauseMixedSyntax) {
    // Test mixing traditional and NTTP syntax
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::age) > 25)          // Using compile-time syntax
                           .where(field(&Author::is_active) == true) // NTTP syntax
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_GT(author.age, 25);
        EXPECT_TRUE(author.is_active);
    }
}

TEST_F(ORMTest, WhereClauseNTTPConstCharPtr) {
    // Test NTTP with const char* (C-style string)
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::name) == "Charlie Brown") // const char* literal
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "Charlie Brown");
    EXPECT_EQ(authorsValue[0].age, 30);
}

TEST_F(ORMTest, WhereClauseNTTPWithArithmeticTypes) {
    // Test NTTP with various arithmetic types (now works without SFINAE
    // constraint)
    auto authorsInt = QuerySet<Author>(conn)
                              .where(field(&Author::age) == 30) // int
                              .select_all();

    auto authorsDouble = QuerySet<Author>(conn)
                                 .where(field(&Author::rating) == 4.5) // double
                                 .select_all();

    auto authorsFloat = QuerySet<Author>(conn)
                                .where(field(&Author::score) == 85.5f) // float
                                .select_all();

    ASSERT_TRUE(authorsInt.has_value()) << "Select should return a value" << authorsInt.error();
    ASSERT_EQ(authorsInt.value().size(), 1);
    auto authorsIntValue = authorsInt.value();

    ASSERT_TRUE(authorsDouble.has_value()) << "Select should return a value" << authorsDouble.error();
    ASSERT_EQ(authorsDouble.value().size(), 1);
    auto authorsDoubleValue = authorsDouble.value();

    ASSERT_TRUE(authorsFloat.has_value()) << "Select should return a value" << authorsFloat.error();
    ASSERT_EQ(authorsFloat.value().size(), 1);
    auto authorsFloatValue = authorsFloat.value();
}

TEST_F(ORMTest, WhereClauseNTTPComparisonWithTraditional) {
    // Verify NTTP and traditional syntax produce identical results
    auto authorsTraditional = QuerySet<Author>(conn).where(field(&Author::age) == 30).select_all();

    ASSERT_TRUE(authorsTraditional.has_value()) << "Select should return a value" << authorsTraditional.error();
    ASSERT_EQ(authorsTraditional.value().size(), 1);
    auto authorsTraditionalValue = authorsTraditional.value();

    auto authorsNTTP = QuerySet<Author>(conn).where(field(&Author::age) == 30).select_all();

    ASSERT_TRUE(authorsNTTP.has_value()) << "Select should return a value" << authorsNTTP.error();
    ASSERT_EQ(authorsTraditional.value().size(), authorsNTTP.value().size());
    ASSERT_EQ(authorsTraditional.value().size(), 1);

    // Both should return the same results
    for (size_t i = 0; i < authorsTraditional.value().size(); ++i) {
        EXPECT_EQ(authorsTraditional.value()[i].name, authorsNTTP.value()[i].name);
        EXPECT_EQ(authorsTraditional.value()[i].age, authorsNTTP.value()[i].age);
        EXPECT_EQ(authorsTraditional.value()[i].is_active, authorsNTTP.value()[i].is_active);
    }
}

TEST_F(ORMTest, WhereClauseStringComparison) {
    Author author("John Doe", 30, "john@example.com");
    QuerySet<Author>(conn).insert(author);

    Author author2("John Doe Smith", 31, "john.smith@example.com");
    QuerySet<Author>(conn).insert(author2);

    auto authors = QuerySet<Author>(conn).where(field(&Author::name).like("John%")).select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto authorsValue = authors.value();
    for (const auto& foundAuthor : authorsValue) {
        EXPECT_TRUE(foundAuthor.name.substr(0, 4) == "John");
    }
}

TEST_F(ORMTest, WhereClauseLessOrEqualOperator) {
    // Test LESS_OR_EQUAL operator
    auto authors = QuerySet<Author>(conn).where(field(&Author::age) <= 30).select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_LE(author.age, 30);
    }
}

TEST_F(ORMTest, WhereClauseGreaterOrEqualOperator) {
    // Test GREATER_OR_EQUAL operator
    auto authors = QuerySet<Author>(conn).where(field(&Author::age) >= 25).select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_GE(author.age, 25);
    }
}

TEST_F(ORMTest, WhereClauseNTTPLessOrEqual) {
    // Test LESS_OR_EQUAL with NTTP syntax
    auto authors = QuerySet<Author>(conn).where(field(&Author::age) <= 35).select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_LE(author.age, 35);
    }
}

TEST_F(ORMTest, WhereClauseNTTPGreaterOrEqual) {
    // Test GREATER_OR_EQUAL with NTTP syntax
    auto authors = QuerySet<Author>(conn).where(field(&Author::rating) >= 3.0).select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);
    auto authorsValue = authors.value();
    for (const auto& author : authorsValue) {
        EXPECT_GE(author.rating, 3.0);
    }
}

TEST_F(ORMTest, WhereClauseIsOperator) {
    // TODO: simplify using insert instead raw sql, i need this for now because i
    // dont have mechanism to insert NULL values Insert an author with NULL
    // biography directly using SQL
    auto stmt = Statement(
            conn,
            "INSERT INTO author (name, age, email, "
            "biography) VALUES (?, ?, ?, NULL)"
    );
    stmt.bind(1, "John Doe");
    stmt.bind(2, 30);
    stmt.bind(3, "john@example.com");
    stmt.execute();

    // Execute the query using our ORM
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::biography) == std::nullopt) // WHERE biography IS NULL
                           .select_all();

    // Verify we found the author with NULL biography
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "John Doe");
    EXPECT_EQ(authorsValue[0].age, 30);
    EXPECT_EQ(authorsValue[0].email, "john@example.com");
}

TEST_F(ORMTest, WhereClauseNTTPIsOperator) {
    // Test IS operator with NTTP syntax
    // TODO: simplify using insert instead raw sql, i need this for now because i
    // dont have mechanism to insert NULL values Insert an author with NULL
    // biography directly using SQL
    auto stmt = Statement(
            conn,
            "INSERT INTO author (name, age, email, "
            "biography) VALUES (?, ?, ?, NULL)"
    );
    stmt.bind(1, "John Doe");
    stmt.bind(2, 30);
    stmt.bind(3, "john@example.com");
    stmt.execute();

    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::biography) == std::nullopt) // WHERE biography IS NULL
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "John Doe");
    EXPECT_EQ(authorsValue[0].age, 30);
    EXPECT_EQ(authorsValue[0].email, "john@example.com");
}

TEST_F(ORMTest, WhereNotNull) {
    // Test where_not_null with parameter-based syntax
    auto authors = QuerySet<Author>(conn).where(field(&Author::biography) != nullptr).select_all();

    // Verify we found only authors with non-NULL biography
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4);

    // Check that all returned authors have non-NULL biography
    for (const auto& author : authors.value()) {
        ASSERT_FALSE(author.biography.empty());
    }
}

TEST_F(ORMTest, WhereNotNullWithAdditionalConditions) {
    // Test where_not_null combined with other conditions
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::biography) != nullptr)
                           .where(field(&Author::age) >= 30)
                           .select_all();

    // Verify we found only authors with non-NULL biography AND age >= 30
    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_GT(authors.value().size(), 0);

    // Check that all returned authors have non-NULL biography and age >= 30
    for (const auto& author : authors.value()) {
        ASSERT_FALSE(author.biography.empty());
        ASSERT_GE(author.age, 30);
    }
}

TEST_F(ORMTest, ComparisonWithExplicitCondition) {
    // Test where_not_null against equivalent explicit condition
    auto authors1 = QuerySet<Author>(conn).where(field(&Author::biography) != nullptr).select_all();

    auto authors2 = QuerySet<Author>(conn).where(field(&Author::biography) != std::nullopt).select_all();

    // Both queries should return the same results
    ASSERT_TRUE(authors1.has_value()) << "First query should return a value" << authors1.error();
    ASSERT_TRUE(authors2.has_value()) << "Second query should return a value" << authors2.error();
    ASSERT_GT(authors1.value().size(), 0);

    // Both should have the same number of results
    ASSERT_EQ(authors1.value().size(), authors2.value().size());
}

TEST_F(ORMTest, UnifiedWhereInterfaceDemo) {
    // Demonstrate the new unified where() interface with all overloads

    // 1. Basic equality (default op) - where(field, value)
    auto authors1 = QuerySet<Author>(conn).where(field(&Author::age) == 30).select_all();
    ASSERT_TRUE(authors1.has_value());

    // 2. Explicit operator - where(field, value, op)
    auto authors2 = QuerySet<Author>(conn).where(field(&Author::name).like("John%")).select_all();
    ASSERT_TRUE(authors2.has_value());

    // 3. Between (two values) - where(field >= value1 and field <= value2)
    auto authors3 = QuerySet<Author>(conn).where(field(&Author::age) >= 25 and field(&Author::age) <= 35).select_all();
    ASSERT_TRUE(authors3.has_value());
    ASSERT_GE(authors3.value().size(), 1);

    // 4. NULL checks - where(field, Op::IS) / where(field, Op::IS_NOT)
    auto authors4 = QuerySet<Author>(conn).where(field(&Author::biography) != std::nullopt).select_all();
    ASSERT_TRUE(authors4.has_value());

    auto authors5 = QuerySet<Author>(conn).where(field(&Author::biography) == std::nullopt).select_all();
    ASSERT_TRUE(authors5.has_value());

    // 5. IN clause - where(field.in(container))
    std::vector<int> ages{25, 30, 35};
    auto             authors6 = QuerySet<Author>(conn).where(field(&Author::age).in(ages)).select_all();
    ASSERT_TRUE(authors6.has_value());
    ASSERT_GE(authors6.value().size(), 1);

    // Verify all authors returned have ages in the specified range
    for (const auto& author : authors6.value()) {
        EXPECT_TRUE(author.age == 25 || author.age == 30 || author.age == 35);
    }
}

TEST_F(ORMTest, WhereClauseOrOperator) {
    // Test: age = 25 OR age = 35 (Alice OR Bob)
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::age) == 25 or field(&Author::age) == 35) // NOSONAR
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto                               authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
}

TEST_F(ORMTest, WhereClauseAndOperator) {
    // Test: age >= 30 AND rating >= 4.5 (Only Bob - Diana is 28, Charlie is 30
    // but rating 4.0)
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::age) >= 30 and field(&Author::rating) >= 4.5) // NOSONAR
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 1);
    auto authorsValue = authors.value();
    EXPECT_EQ(authorsValue[0].name, "Bob Johnson");
}

TEST_F(ORMTest, WhereClauseComplexAndOr) {
    // Test: (age = 25 OR age = 28) AND rating >= 4.5 (Alice AND Diana)
    auto authors = QuerySet<Author>(conn)
                           .where((field(&Author::age) == 25 or field(&Author::age) == 28) and
                                  field(&Author::rating) >= 4.5) // NOSONAR
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto                               authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseComplexOrAnd) {
    // Test: age = 25 OR (age >= 30 AND rating = 5.0) (Alice OR Bob)
    auto authors = QuerySet<Author>(conn)
                           .where((field(&Author::age) == 25 or
                                   (field(&Author::age) >= 30 and field(&Author::rating) == 5.0))) // NOSONAR
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto                               authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
}

TEST_F(ORMTest, WhereClauseTripleAnd) {
    // Test: age >= 25 AND age <= 30 AND rating >= 4.0 (Alice, Charlie, Diana)
    // Alice: 25, 4.5; Charlie: 30, 4.0; Diana: 28, 5.5
    auto authors = QuerySet<Author>(conn)
                           .where((field(&Author::age) >= 25 and field(&Author::age) <= 30 and
                                   field(&Author::rating) >= 4.0)) // NOSONAR
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto                               authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Charlie Brown"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseTripleOr) {
    // Test: age = 25 OR age = 30 OR age = 35 (Alice, Charlie, Bob)
    auto authors = QuerySet<Author>(conn)
                           .where((field(&Author::age) == 25 or field(&Author::age) == 30 or
                                   field(&Author::age) == 35)) // NOSONAR
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto                               authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Charlie Brown"));
    EXPECT_TRUE(names.count("Bob Johnson"));
}

TEST_F(ORMTest, WhereClauseNestedComplexGrouping) {
    // Test: (age = 25 OR age = 35) AND (rating >= 4.5 OR score >= 90.0)
    // Should match: Alice (25, 4.5) and Bob (35, 5.0, 90.0)
    auto authors = QuerySet<Author>(conn)
                           .where((field(&Author::age) == 25 or field(&Author::age) == 35) and      // NOSONAR
                                  (field(&Author::rating) >= 4.5 or field(&Author::score) >= 90.0)) // NOSONAR
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto                               authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
}

TEST_F(ORMTest, WhereClauseComplexNestedConditions) {
    // Test: ((age < 30 AND rating > 4.0) OR (age >= 30 AND score >= 85.0)) AND
    // is_active = true Should match: Alice (25, 4.5, active), Bob (35, 90.0,
    // active), Diana (28, 5.5, 95.0, active)
    auto authors = QuerySet<Author>(conn)
                           .where(((field(&Author::age) < 30 and field(&Author::rating) > 4.0) or // NOSONAR
                                   (field(&Author::age) >= 30 and field(&Author::score) >= 85.0)) and
                                  field(&Author::is_active) == true) // NOSONAR
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto                               authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseMixedDataTypes) {
    // Test with different data types: string, int, double, bool
    // Test: name LIKE 'Alice%' OR (age > 30 AND rating >= 4.0 AND is_active =
    // true)
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::name).like("Alice%") or // NOSONAR
                                  (field(&Author::age) > 30 and field(&Author::rating) >= 4.0 and
                                   field(&Author::is_active) == true)) // NOSONAR
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto                               authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
}

TEST_F(ORMTest, WhereClauseGroupMethod) {
    // Test using the group() method for explicit grouping
    // Test: (age = 25 OR age = 28) AND rating >= 4.5
    auto authors = QuerySet<Author>(conn)
                           .where((field(&Author::age) == 25 or field(&Author::age) == 28) and
                                  field(&Author::rating) >= 4.5) // NOSONAR
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto                               authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseStringOperations) {
    // Test string operations with complex conditions
    // Test: (name LIKE '%Brown' OR email LIKE '%alice%') AND age >= 25
    auto authors = QuerySet<Author>(conn)
                           .where((field(&Author::name).like("%Brown") or field(&Author::email).like("%alice%")) and
                                  field(&Author::age) >= 25) // NOSONAR
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto                               authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Charlie Brown"));
}

TEST_F(ORMTest, WhereClauseFloatComparisons) {
    // Test float/double comparisons with complex logic
    // Test: (score >= 85.0 AND score <= 90.0) OR rating > 5.0
    // Should match: Alice (85.5), Bob (90.0), Diana (95.0, 5.5)
    auto authors = QuerySet<Author>(conn)
                           .where((field(&Author::score) >= 85.0 and field(&Author::score) <= 90.0) or
                                  field(&Author::rating) > 5.0) // NOSONAR
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto                               authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseNotEqualsOperator) {
    // Test NOT_EQUALS with complex conditions
    // Test: age != 30 AND (rating >= 4.5 OR score >= 90.0)
    // Should match: Alice (25, 4.5), Bob (35, 5.0, 90.0), Diana (28, 5.5, 95.0)
    auto authors = QuerySet<Author>(conn)
                           .where((field(&Author::age) != 30 and
                                   (field(&Author::rating) >= 4.5 or field(&Author::score) >= 90.0))) // NOSONAR
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 3);
    auto                               authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseComplexChaining) {
    // Test complex chaining of multiple conditions
    // Test: (age >= 25 AND age <= 30) OR (rating >= 5.0 AND score >= 90.0) OR
    // name LIKE '%Prince' Should match: Alice (25), Charlie (30), Diana (28), Bob
    // (5.0, 90.0), Diana (name ends with Prince) Note: Diana matches multiple
    // conditions
    auto authors = QuerySet<Author>(conn)
                           .where(((field(&Author::age) >= 25 and field(&Author::age) <= 30) or         // NOSONAR
                                   (field(&Author::rating) >= 5.0 and field(&Author::score) >= 90.0) or // NOSONAR
                                   field(&Author::name).like("%Prince")))
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 4); // All authors should match
    auto                               authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Bob Johnson"));
    EXPECT_TRUE(names.count("Charlie Brown"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseBooleanLogic) {
    // Test boolean field with complex conditions
    // Test: is_active = true AND ((age < 30 AND rating > 4.0) OR score >= 95.0)
    // Should match: Alice (active, 25, 4.5), Diana (active, 28, 5.5, 95.0)
    auto authors = QuerySet<Author>(conn)
                           .where((field(&Author::is_active) == true and                           // NOSONAR
                                   ((field(&Author::age) < 30 and field(&Author::rating) > 4.0) or // NOSONAR
                                    field(&Author::score) >= 95.0)))
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 2);
    auto                               authorsValue = authors.value();
    std::set<std::string, std::less<>> names;
    for (const auto& author : authorsValue) {
        names.insert(author.name);
    }
    EXPECT_TRUE(names.count("Alice Smith"));
    EXPECT_TRUE(names.count("Diana Prince"));
}

TEST_F(ORMTest, WhereClauseEmptyResult) {
    // Test condition that should return no results
    // Test: age > 100 OR (rating < 0 AND is_active = false)
    auto authors = QuerySet<Author>(conn)
                           .where(field(&Author::age) > 100 or                                         // NOSONAR
                                  (field(&Author::rating) < 0 and field(&Author::is_active) == false)) // NOSONAR
                           .select_all();

    ASSERT_TRUE(authors.has_value()) << "Select should return a value" << authors.error();
    ASSERT_EQ(authors.value().size(), 0);
}

// =======================================
// ONLY TESTS
// =======================================
TEST_F(ORMTest, SelectOnlyFields) {
    // Test selecting only specific fields and verify field filtering
    auto multiFieldAuthors =
            QuerySet<Author>(conn).only(field(&Author::name), field(&Author::age), field(&Author::email)).select_all();
    auto twoFieldAuthors = QuerySet<Author>(conn).only(field(&Author::name), field(&Author::age)).select_all();

    // Verify both queries succeed
    ASSERT_TRUE(multiFieldAuthors.has_value()) << multiFieldAuthors.error();
    ASSERT_TRUE(twoFieldAuthors.has_value()) << twoFieldAuthors.error();
    ASSERT_EQ(multiFieldAuthors.value().size(), 4);
    ASSERT_EQ(twoFieldAuthors.value().size(), 4);

    // Check multi-field results
    for (const auto& author : multiFieldAuthors.value()) {
        EXPECT_FALSE(author.name.empty());
        EXPECT_GT(author.age, 0);
        EXPECT_FALSE(author.email.empty());
        // Non-selected fields should have default values
        EXPECT_EQ(author.id, 0);
        EXPECT_EQ(author.rating, 0.0);
        EXPECT_TRUE(author.biography.empty());
    }

    // Check two-field results
    for (const auto& author : twoFieldAuthors.value()) {
        EXPECT_FALSE(author.name.empty());
        EXPECT_GT(author.age, 0);
        // Non-selected fields should have default values
        EXPECT_TRUE(author.email.empty());
        EXPECT_EQ(author.id, 0);
    }
}

TEST_F(ORMTest, SelectValues) {
    // Test the select_values method for dictionary-like access
    auto result = QuerySet<Author>(conn)
                          .only(field(&Author::name), field(&Author::age), field(&Author::is_active))
                          .select_values();

    // Check if we got a valid result
    ASSERT_TRUE(result.has_value()) << "select_values failed with error: " << result.error();

    // Define expected results
    ValueVectorMap expected_results =
            {{{"name", "Alice Smith"}, {"age", 25}, {"is_active", true}},
             {{"name", "Bob Johnson"}, {"age", 35}, {"is_active", true}},
             {{"name", "Charlie Brown"}, {"age", 30}, {"is_active", true}},
             {{"name", "Diana Prince"}, {"age", 28}, {"is_active", true}}};

    // Use the AssertResultsMatch helper to verify the results
    AssertResultsMatch(result, expected_results);
}

// =======================================
// DISTINCT TESTS
// =======================================
TEST_F(ORMTest, DistinctTemplateBased) {
    // Setup: Insert duplicate age values
    Author duplicateAge("Frank Miller", 30, "frank@example.com");
    QuerySet<Author>(conn).insert(duplicateAge);

    // First, get all authors without distinct
    auto allAuthors = QuerySet<Author>(conn).select_all();

    // Then, get distinct ages directly using select_values
    auto distinctAgeValues = QuerySet<Author>(conn).distinct(field(&Author::age)).select_values();

    // Check if we got a valid result
    ASSERT_TRUE(distinctAgeValues.has_value()) << "select_values failed with error: " << distinctAgeValues.error();

    ASSERT_TRUE(allAuthors.has_value()) << "select_all failed with error: " << allAuthors.error();
    // We should have 5 total authors (Alice, Bob, Charlie, Diana, Frank)
    ASSERT_EQ(allAuthors.value().size(), 5);

    // With our implementation, we should get 4 distinct ages
    ASSERT_EQ(distinctAgeValues.value().size(), 4);

    // Create a set of all ages to verify all ages are present
    std::set<int> allAgesSet;
    for (const auto& author : allAuthors.value()) {
        allAgesSet.insert(author.age);
    }

    // Extract the ages from the values and convert to a set
    std::set<int> distinctAgesSet;
    for (const auto& row : distinctAgeValues.value()) {
        ASSERT_TRUE(row.count("age"));
        ASSERT_TRUE(std::holds_alternative<int>(row.at("age")));
        distinctAgesSet.insert(std::get<int>(row.at("age")));
    }

    // Both sets should have the same 4 distinct ages
    ASSERT_EQ(allAgesSet.size(), 4);
    ASSERT_EQ(distinctAgesSet.size(), 4);

    // Verify we have all expected ages
    ASSERT_TRUE(distinctAgesSet.contains(25)); // Alice
    ASSERT_TRUE(distinctAgesSet.contains(28)); // Diana
    ASSERT_TRUE(distinctAgesSet.contains(30)); // Charlie and Frank (both should be returned)
    ASSERT_TRUE(distinctAgesSet.contains(35)); // Bob

    // Count how many authors have age 30 (should be 2)
    int age30Count = 0;
    for (const auto& author : allAuthors.value()) {
        if (author.age == 30)
            age30Count++;
    }
    ASSERT_EQ(age30Count, 2); // Both Charlie and Frank have age 30
}

TEST_F(ORMTest, DistinctMultipleFields) {
    // Setup: Insert authors with duplicate combinations
    Author duplicate1("Greg Smith", 25, "greg@example.com");   // Same age as Alice
    Author duplicate2("Helen Smith", 35, "helen@example.com"); // Same age as Bob
    Author duplicate3("Alice Clone", 25, "clone@example.com", 0, true,
                      4.5); // Same age and rating as Alice

    QuerySet<Author>(conn).insert(duplicate1);
    QuerySet<Author>(conn).insert(duplicate2);
    QuerySet<Author>(conn).insert(duplicate3);

    // Test distinct with multiple fields - use select_values() instead of
    // select_all() since distinct queries return only the specified fields, not
    // complete objects
    auto distinctValues = QuerySet<Author>(conn).distinct(field(&Author::age), field(&Author::rating)).select_values();

    // Check if we got a valid result
    ASSERT_TRUE(distinctValues.has_value()) << "select_values failed with error: " << distinctValues.error();

    // Count distinct combinations of age and rating
    std::set<std::pair<int, double>> distinctCombinations;
    for (const auto& row : distinctValues.value()) {
        // Extract age and rating from the returned values
        ASSERT_TRUE(row.count("age"));
        ASSERT_TRUE(row.count("rating"));

        // With our improved implementation, we should get the correct types
        int    age    = std::get<int>(row.at("age"));
        double rating = std::get<double>(row.at("rating"));
        distinctCombinations.insert({age, rating});
    }

    // Verify we have at least these distinct combinations
    // Alice and clone (25, 4.5), Greg (25, 0.0), Bob and Helen (35, 5.0), Charlie
    // (30, 4.0), Diana (28, 5.5) Note: Previous tests may have added additional
    // authors with different combinations
    ASSERT_GE(distinctCombinations.size(), 5);
    ASSERT_TRUE(distinctCombinations.contains(std::make_pair(25, 4.5))); // Alice and clone
    ASSERT_TRUE(distinctCombinations.contains(std::make_pair(25, 0.0))); // Greg
    ASSERT_TRUE(distinctCombinations.contains(std::make_pair(35, 5.0))); // Bob and Helen
    ASSERT_TRUE(distinctCombinations.contains(std::make_pair(30, 4.0))); // Charlie
    ASSERT_TRUE(distinctCombinations.contains(std::make_pair(28, 5.5))); // Diana
}

TEST_F(ORMTest, DistinctWithOnlyFields) {
    // Setup: Insert authors with duplicate combinations
    Author duplicate1("Greg Smith", 25, "greg@example.com");   // Same age as Alice
    Author duplicate2("Helen Smith", 35, "helen@example.com"); // Same age as Bob
    Author duplicate3("Alice Clone", 25, "clone@example.com", 0, true,
                      4.5); // Same age and rating as Alice

    QuerySet<Author>(conn).insert(duplicate1);
    QuerySet<Author>(conn).insert(duplicate2);
    QuerySet<Author>(conn).insert(duplicate3);

    // Test distinct with only specific fields
    auto distinctValues = QuerySet<Author>(conn)
                                  .distinct(field(&Author::age))
                                  .only(field(&Author::age), field(&Author::name))
                                  .select_values();

    // Check if we got a valid result
    ASSERT_TRUE(distinctValues.has_value()) << "select_values failed with error: " << distinctValues.error();

    // When using distinct with only, we get distinct combinations of the fields
    // So we'll have 7 rows (all unique combinations of age and name)
    ASSERT_EQ(distinctValues.value().size(), 7);

    // Each row should contain only age and name fields
    for (const auto& row : distinctValues.value()) {
        // Should have exactly 2 fields
        ASSERT_EQ(row.size(), 2);

        // Should have age and name fields
        ASSERT_TRUE(row.count("age"));
        ASSERT_TRUE(row.count("name"));

        // Should not have other fields
        ASSERT_FALSE(row.count("email"));
        ASSERT_FALSE(row.count("id"));
        ASSERT_FALSE(row.count("rating"));
    }

    // Extract the ages from the values
    std::set<int> distinctAgesSet;
    for (const auto& row : distinctValues.value()) {
        ASSERT_TRUE(std::holds_alternative<int>(row.at("age")));
        distinctAgesSet.insert(std::get<int>(row.at("age")));
    }

    // Verify we have all expected ages
    ASSERT_EQ(distinctAgesSet.size(), 4);
    ASSERT_TRUE(distinctAgesSet.contains(25)); // Alice, Greg, and clone
    ASSERT_TRUE(distinctAgesSet.contains(28)); // Diana
    ASSERT_TRUE(distinctAgesSet.contains(30)); // Charlie
    ASSERT_TRUE(distinctAgesSet.contains(35)); // Bob and Helen
}

TEST_F(ORMTest, DistinctWithWhere) {
    // Setup: Insert authors with various ages
    Author author1("Jack", 40, "jack@example.com");
    Author author2("Kate", 40, "kate@example.com");
    Author author3("Luke", 45, "luke@example.com");

    QuerySet<Author>(conn).insert(author1);
    QuerySet<Author>(conn).insert(author2);
    QuerySet<Author>(conn).insert(author3);

    // Test distinct with where clause
    auto authorResults =
            QuerySet<Author>(conn).where(field(&Author::age) >= 35).distinct(field(&Author::age)).select_values();

    // Check if we got a valid result
    ASSERT_TRUE(authorResults.has_value()) << "select_values failed with error: " << authorResults.error();

    // We should have at least 3 distinct ages (35, 40, 45) that are >= 35
    // Note: Other tests may have added additional authors with ages >= 35
    ASSERT_GE(authorResults.value().size(), 3);

    // Create a set of ages to verify uniqueness
    std::set<int> distinctAges;
    for (const auto& result : authorResults.value()) {
        // With select_values, we get a map with field name as key
        distinctAges.insert(
                std::visit(
                        [](auto&& arg) -> int {
                            using ArgType = std::decay_t<decltype(arg)>;
                            if constexpr (std::is_same_v<ArgType, int>) {
                                return arg;
                            } else if constexpr (std::is_same_v<ArgType, std::monostate>) {
                                return 0;
                            } else if constexpr (std::is_arithmetic_v<ArgType>) {
                                return static_cast<int>(arg);
                            } else {
                                return 0;
                            }
                        },
                        result.at("age")
                )
        );
    }

    // Verify we have at least these 3 distinct ages
    ASSERT_GE(distinctAges.size(), 3);
    ASSERT_TRUE(distinctAges.contains(35)); // Bob, Eve, Ian
    ASSERT_TRUE(distinctAges.contains(40)); // Jack and Kate (but only counted once)
    ASSERT_TRUE(distinctAges.contains(45)); // Luke
}

// =======================================
// RAW SQL TESTS
// =======================================
// TODO: Fix get_member_names_if implementation
/* TEST_F(ORMTest, RawSqlFromStatementInsert) {
    // Create a new author for testing
    Author testAuthor("Test Author", 40, "test@example.com");

    // Get the Statement object without executing it
    auto stmt = QuerySet<Author>(conn).stmt_insert(testAuthor);

    // Verify that we can get the raw SQL
    std::string rawSql = stmt.sql();

    // Check the exact SQL string format
    // The SQL should be in the format: INSERT INTO author (name, age, email,
    // is_active, rating, score, middleName, biography) VALUES (?, ?, ?, ?, ?, ?,
    // ?, ?) RETURNING id;
    std::string expectedSqlPattern = "INSERT INTO author (";
    ASSERT_TRUE(rawSql.find(expectedSqlPattern) == 0)
            << "SQL should start with '" << expectedSqlPattern << "' but got: " << rawSql;

    // Check for field names in the SQL
    ASSERT_TRUE(rawSql.find("name") != std::string::npos) << "SQL should contain 'name' field";
    ASSERT_TRUE(rawSql.find("age") != std::string::npos) << "SQL should contain 'age' field";
    ASSERT_TRUE(rawSql.find("email") != std::string::npos) << "SQL should contain 'email' field";

    // Check for VALUES clause
    ASSERT_TRUE(rawSql.find(") VALUES (") != std::string::npos)
            << "SQL should contain ') VALUES (' but got: " << rawSql;

    // Check for placeholders
    size_t questionMarkCount = 0;
    for (char c : rawSql) {
        if (c == '?')
            questionMarkCount++;
    }
    ASSERT_EQ(questionMarkCount, 8) << "Expected 8 placeholders but found " << questionMarkCount;

    // Check for RETURNING clause
    ASSERT_TRUE(rawSql.find("RETURNING id") != std::string::npos)
            << "SQL should contain 'RETURNING id' but got: " << rawSql;

    // Now execute the insert and verify it worked
    auto authorId = QuerySet<Author>(conn).insert(testAuthor);
    ASSERT_TRUE(authorId.has_value()) << "insert failed with error: " << authorId.error();
    ASSERT_GT(authorId.value(), 0);

    // Test with multiple objects
    std::vector<Author> authors =
            {Author("Multi Author 1", 41, "multi1@example.com"), Author("Multi Author 2", 42, "multi2@example.com")};

    // Get the Statement object for multiple inserts
    auto multiStmt = QuerySet<Author>(conn).stmt_insert(authors);

    // Verify the SQL for multiple inserts
    std::string multiRawSql = multiStmt.sql();

    // Check the exact SQL string format for multiple inserts
    ASSERT_TRUE(multiRawSql.find(expectedSqlPattern) == 0)
            << "Multi SQL should start with '" << expectedSqlPattern << "' but got: " << multiRawSql;

    // Check for VALUES with multiple value sets
    ASSERT_TRUE(multiRawSql.find(") VALUES (?") != std::string::npos)
            << "SQL should contain ') VALUES (?' but got: " << multiRawSql;

    // Check for multiple sets of placeholders
    questionMarkCount = 0;
    for (char c : multiRawSql) {
        if (c == '?')
            questionMarkCount++;
    }
    ASSERT_EQ(questionMarkCount, 16) << "Expected 16 placeholders (8 per author) but found " << questionMarkCount;

    // Execute the multiple inserts and verify
    auto ids = QuerySet<Author>(conn).insert(authors);
    ASSERT_TRUE(ids.has_value()) << "insert failed with error: " << ids.error();
    ASSERT_EQ(ids.value().size(), 2);
    ASSERT_GT(ids.value()[0], 0);
    ASSERT_GT(ids.value()[1], 0);
} */

// =======================================
// GROUP BY TESTS
// =======================================

TEST_F(ORMTest, GroupByBasic) {
    // Add more authors with the same age for testing group by
    Author eve("Eve Johnson", 35, "eve@example.com");
    Author frank("Frank Miller", 40, "frank@example.com");
    Author grace("Grace Lee", 25, "grace@example.com");
    Author henry("Henry Wilson", 40, "henry@example.com");
    Author ian("Ian Davis", 35, "ian@example.com");

    QuerySet<Author>(conn).insert(eve);
    QuerySet<Author>(conn).insert(frank);
    QuerySet<Author>(conn).insert(grace);
    QuerySet<Author>(conn).insert(henry);
    QuerySet<Author>(conn).insert(ian);

    // Use group_by to get distinct ages
    auto results = QuerySet<Author>(conn).group_by(field(&Author::age)).select_all();

    // We should have 4 distinct age groups: 25, 28, 30, 35, 40
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 5);

    // Verify the ages are present in the results
    std::set<int> distinctAges;
    for (const auto& author : results.value()) {
        distinctAges.insert(author.age);
    }

    ASSERT_EQ(distinctAges.size(), 5);
    ASSERT_TRUE(distinctAges.contains(25));
    ASSERT_TRUE(distinctAges.contains(28));
    ASSERT_TRUE(distinctAges.contains(30));
    ASSERT_TRUE(distinctAges.contains(35));
    ASSERT_TRUE(distinctAges.contains(40));
}

TEST_F(ORMTest, GroupByMultipleFields) {
    // Add more authors with combinations of age and rating for testing group by
    Author jack("Jack Thompson", 40, "jack@example.com", 0, true, 4.0);
    Author kate("Kate Williams", 40, "kate@example.com", 0, true, 4.5);
    Author luke("Luke Brown", 45, "luke@example.com", 0, true, 4.0);

    QuerySet<Author>(conn).insert(jack);
    QuerySet<Author>(conn).insert(kate);
    QuerySet<Author>(conn).insert(luke);

    // Group by both age and rating
    auto results = QuerySet<Author>(conn).group_by(field(&Author::age), field(&Author::rating)).select_all();

    // We should have at least these combinations:
    // (25, 4.5) - Alice
    // (35, 5.0) - Bob
    // (30, 4.0) - Charlie
    // (28, 5.5) - Diana
    // (40, 4.0) - Jack
    // (40, 4.5) - Kate
    // (45, 4.0) - Luke
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_GE(results.value().size(), 7);

    // Create a set of age-rating pairs to verify unique combinations
    std::set<std::pair<int, double>> ageRatingPairs;
    for (const auto& author : results.value()) {
        ageRatingPairs.insert({author.age, author.rating});
    }

    // Verify specific combinations
    ASSERT_TRUE(ageRatingPairs.contains({25, 4.5})); // Alice
    ASSERT_TRUE(ageRatingPairs.contains({35, 5.0})); // Bob
    ASSERT_TRUE(ageRatingPairs.contains({30, 4.0})); // Charlie
    ASSERT_TRUE(ageRatingPairs.contains({40, 4.0})); // Jack
    ASSERT_TRUE(ageRatingPairs.contains({40, 4.5})); // Kate
    ASSERT_TRUE(ageRatingPairs.contains({45, 4.0})); // Luke
}

TEST_F(ORMTest, GroupByWithOrderBy) {
    // Add more authors for testing
    Author mark("Mark Johnson", 35, "mark@example.com", 0, true, 3.5);
    Author nancy("Nancy Davis", 40, "nancy@example.com", 0, true, 3.0);

    QuerySet<Author>(conn).insert(mark);
    QuerySet<Author>(conn).insert(nancy);

    // Group by age and order by age descending
    auto results =
            QuerySet<Author>(conn).group_by(field(&Author::age)).order_by(field(&Author::age), false).select_all();

    // Extract ages to verify ordering
    std::vector<int> ages;
    for (const auto& author : results.value()) {
        ages.push_back(author.age);
    }

    // Verify that ages are in descending order
    ASSERT_FALSE(ages.empty());
    for (size_t i = 1; i < ages.size(); ++i) {
        ASSERT_GE(ages[i - 1], ages[i]) << "Ages should be in descending order";
    }

    // Verify the highest age is first
    ASSERT_EQ(ages[0], 40); // Nancy has the highest age (40)
}

TEST_F(ORMTest, GroupByWithJoinAndWhere) {
    // Add more posts for testing
    Post post5("Another Post by Alice", "More content from Alice", alice_id);
    Post post6("Second Post by Bob", "More content from Bob", bob_id);
    Post post7("Another Post by Charlie", "More content from Charlie", charlie_id);

    QuerySet<Post>(conn).insert(post5);
    QuerySet<Post>(conn).insert(post6);
    QuerySet<Post>(conn).insert(post7);

    // Group posts by author_id where the author's age is greater than 25
    auto results = QuerySet<Post>(conn)
                           .join(field(&Post::author_id))
                           .where(field(&Author::age) > 25)
                           .group_by(field(&Author::age))
                           .select_all();

    // We should have 3 groups: Bob (35), Charlie (30), Diana (28)
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 3);

    // Extract author IDs directly from Author objects
    std::set<int> authorIds;
    for (const auto& post : results.value()) {
        authorIds.insert(post.author_id);
    }

    // Verify the correct author IDs are present
    ASSERT_TRUE(authorIds.contains(bob_id));
    ASSERT_TRUE(authorIds.contains(charlie_id));
    ASSERT_TRUE(authorIds.contains(diana_id));
    ASSERT_FALSE(authorIds.contains(alice_id)); // Alice's age is 25, not > 25
}

// =======================================
// TRANSACTION TESTS
// =======================================

TEST_F(ORMTest, BasicTransactionCommit) {
    // Clear existing test data
    clearTestData();

    // Start a transaction
    conn->begin_transaction();

    // Insert an author within the transaction
    Author alice("Alice Smith", 25, "alice@example.com");
    auto   alice_id = QuerySet<Author>(conn).insert(alice);
    ASSERT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();

    // Commit the transaction
    conn->commit();

    // Verify the author was inserted
    auto results = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 1);
    ASSERT_EQ(results.value()[0].name, "Alice Smith");
    ASSERT_EQ(results.value()[0].id, alice_id.value());
}

TEST_F(ORMTest, BasicTransactionRollback) {
    // Clear existing test data
    clearTestData();

    // Start a transaction
    conn->begin_transaction();

    // Insert an author within the transaction
    Author alice("Alice Smith", 25, "alice@example.com");
    auto   alice_id = QuerySet<Author>(conn).insert(alice);
    ASSERT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();

    // Rollback the transaction
    conn->rollback();

    // Verify the author was not inserted
    auto results = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(),
              0); // No authors should exist after rollback
}

TEST_F(ORMTest, TransactionLevels) {
    // Clear existing test data
    clearTestData();

    // Test different transaction isolation levels
    conn->begin_transaction(Connection::TransactionLevel::IMMEDIATE);

    Author alice("Alice Smith", 25, "alice@example.com");
    auto   alice_id = QuerySet<Author>(conn).insert(alice);
    ASSERT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();

    conn->commit();

    // Start another transaction with EXCLUSIVE level
    conn->begin_transaction(Connection::TransactionLevel::EXCLUSIVE);

    Author bob("Bob Johnson", 30, "bob@example.com");
    auto   bob_id = QuerySet<Author>(conn).insert(bob);
    ASSERT_TRUE(bob_id.has_value()) << "query failed with error: " << bob_id.error();

    conn->commit();

    // Verify both authors were inserted
    auto results = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 2);
}

TEST_F(ORMTest, NestedTransactionException) {
    // Clear existing test data
    clearTestData();

    // Start a transaction
    conn->begin_transaction();

    // Attempt to start another transaction while one is active
    ASSERT_THROW(conn->begin_transaction(), TransactionAlreadyActiveException);

    // Clean up
    conn->rollback();
}

TEST_F(ORMTest, CommitWithoutTransactionException) {
    // Clear existing test data
    clearTestData();

    // Attempt to commit without an active transaction
    ASSERT_THROW(conn->commit(), TransactionNotActiveException);
}

TEST_F(ORMTest, RollbackWithoutTransactionException) {
    // Clear existing test data
    clearTestData();

    // Attempt to rollback without an active transaction
    ASSERT_THROW(conn->rollback(), TransactionNotActiveException);
}

TEST_F(ORMTest, TransactionRAIIWrapper) {
    // Clear existing test data
    clearTestData();

    // Test the RAII transaction wrapper
    {
        Transaction tx(conn);

        // Insert an author within the transaction
        Author alice("Alice Smith", 25, "alice@example.com");
        auto   alice_id = QuerySet<Author>(conn).insert(alice);
        ASSERT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();

        // Transaction will be rolled back when it goes out of scope
        // because we don't call commit()
    }

    // Verify the author was not inserted (transaction was rolled back)
    auto results = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(),
              0); // No authors should exist after rollback
}

TEST_F(ORMTest, TransactionRAIICommit) {
    // Clear existing test data
    clearTestData();

    // Use the RAII Transaction wrapper with explicit commit
    {
        Transaction tx(conn);

        // Insert an author within the transaction
        Author alice("Alice Smith", 25, "alice@example.com");
        auto   alice_id = QuerySet<Author>(conn).insert(alice);
        ASSERT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();

        // Explicitly commit the transaction
        tx.commit();
    }

    // Verify the author was inserted
    auto results = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 1);
    ASSERT_EQ(results.value()[0].name, "Alice Smith");
}

TEST_F(ORMTest, TransactionRAIIRollback) {
    // Clear existing test data
    clearTestData();

    // Test the RAII transaction wrapper with explicit rollback
    {
        Transaction tx(conn);

        // Insert an author within the transaction
        Author alice("Alice Smith", 25, "alice@example.com");
        auto   alice_id = QuerySet<Author>(conn).insert(alice);
        ASSERT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();

        // Explicitly rollback the transaction
        tx.rollback();
    }

    // Verify the author was not inserted
    auto results = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(),
              0); // No authors should exist after rollback
}

TEST_F(ORMTest, WithTransactionHelper) {
    // Clear existing test data
    clearTestData();

    // Test the with_transaction helper function
    bool success = with_transaction(conn, [this]() -> bool {
        Author alice("Alice Smith", 25, "alice@example.com");
        auto   alice_id = QuerySet<Author>(this->conn).insert(alice);
        EXPECT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();
        return true;
    });

    ASSERT_TRUE(success);

    // Verify the author was inserted
    auto results = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 1);
    ASSERT_EQ(results.value()[0].name, "Alice Smith");
}

TEST_F(ORMTest, WithTransactionExceptionRollback) {
    // Clear existing test data
    clearTestData();

    // Test that with_transaction rolls back on exception
    try {
        with_transaction(conn, [this]() {
            Author alice("Alice Smith", 25, "alice@example.com");
            auto   alice_id = QuerySet<Author>(this->conn).insert(alice);
            ASSERT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();

            // Throw an exception to trigger rollback
            throw std::runtime_error("Test exception");
        });

        FAIL() << "Expected exception was not thrown";
    } catch (const std::runtime_error& e) {
        ASSERT_STREQ(e.what(), "Test exception");
    }

    // Verify the author was not inserted (transaction was rolled back)
    auto results = QuerySet<Author>(conn).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 0);
}

TEST_F(ORMTest, MultipleOperationsInTransaction) {
    // Clear existing test data
    clearTestData();

    // Test multiple database operations in a single transaction
    conn->begin_transaction();

    // Insert multiple authors
    Author alice("Alice Smith", 25, "alice@example.com");
    Author bob("Bob Johnson", 30, "bob@example.com");
    Author charlie("Charlie Brown", 35, "charlie@example.com");

    auto alice_id = QuerySet<Author>(conn).insert(alice);
    ASSERT_TRUE(alice_id.has_value()) << "query failed with error: " << alice_id.error();
    auto bob_id = QuerySet<Author>(conn).insert(bob);
    ASSERT_TRUE(bob_id.has_value()) << "query failed with error: " << bob_id.error();
    auto charlie_id = QuerySet<Author>(conn).insert(charlie);
    ASSERT_TRUE(charlie_id.has_value()) << "query failed with error: " << charlie_id.error();

    // Update an author - first retrieve the current object with the correct ID
    auto result = QuerySet<Author>(conn).where(field(&Author::id) == alice_id.value()).select_one();
    ASSERT_TRUE(result.has_value()) << "query failed with error: " << result.error();
    auto alice_from_db = result.value();
    alice_from_db.age  = 26;
    auto update_result = QuerySet<Author>(conn).update(alice_from_db);
    ASSERT_TRUE(update_result.has_value()) << "query failed with error: " << update_result.error();

    // Delete an author
    QuerySet<Author>(conn).where(field(&Author::name) == "Charlie Brown").remove();

    conn->commit();

    // Verify the final state
    auto results = QuerySet<Author>(conn).order_by(field(&Author::name)).select_all();
    ASSERT_TRUE(results.has_value()) << "query failed with error: " << results.error();
    ASSERT_EQ(results.value().size(), 2);
    ASSERT_EQ(results.value()[0].name, "Alice Smith");
    ASSERT_EQ(results.value()[0].age, 26);
    ASSERT_EQ(results.value()[1].name, "Bob Johnson");
}

TEST_F(ORMTest, TransferBetweenAuthorsTransaction) {
    // This test is different as it requires the initial test data
    // We don't clear the data here because we need alice_id and bob_id from
    // setupTestData

    // First, let's create a new post for Alice
    Post alicePost("Alice's Original Post", "This post belongs to Alice", alice_id, 0, 100);
    auto result = QuerySet<Post>(conn).insert(alicePost);
    ASSERT_TRUE(result.has_value()) << "query failed with error: " << result.error();
    auto postId = result.value();

    // Now let's transfer the post from Alice to Bob using a transaction
    // to ensure the operation is atomic
    {
        Transaction tx(conn);

        try {
            // 1. Update the post to belong to Bob
            auto post = QuerySet<Post>(conn).where(field(&Post::id) == postId).select_one();
            ASSERT_TRUE(post.has_value()) << "query failed with error: " << post.error();
            auto postValue      = post.value();
            postValue.author_id = bob_id;
            QuerySet<Post>(conn).where(field(&Post::id) == postId).update(postValue);

            // 2. Update some metadata about the post (title to reflect new ownership)
            postValue.title = "Post Transferred to Bob";
            QuerySet<Post>(conn).where(field(&Post::id) == postId).update(postValue);

            // Commit the transaction - both operations succeed atomically
            tx.commit();
        } catch (const std::exception& e) {
            // Transaction will automatically roll back if not committed
            std::cerr << "Error during post transfer: " << e.what() << std::endl;
            FAIL() << "Transaction should not have failed";
        }
    }

    // Verify the post now belongs to Bob
    auto updatedPost = QuerySet<Post>(conn).where(field(&Post::id) == postId).select_one();
    ASSERT_TRUE(updatedPost.has_value()) << "query failed with error: " << updatedPost.error();
    ASSERT_EQ(updatedPost.value().author_id, bob_id);
    ASSERT_EQ(updatedPost.value().title, "Post Transferred to Bob");

    // Demonstrate a failed transaction with automatic rollback
    {
        Transaction tx(conn);

        // 1. Update the post to belong to Charlie
        auto post = QuerySet<Post>(conn).where(field(&Post::id) == postId).select_one();
        ASSERT_TRUE(post.has_value()) << "query failed with error: " << post.error();
        auto postValue      = post.value();
        postValue.author_id = charlie_id;
        QuerySet<Post>(conn).where(field(&Post::id) == postId).update(postValue);

        // 2. This update will fail because we're using a non-existent author ID
        // (deliberately causing an error to demonstrate rollback)
        postValue.author_id = 9999; // Non-existent author ID

        // We don't commit, so the transaction will be rolled back
        // when it goes out of scope
    }

    // Verify the post still belongs to Bob (rollback worked)
    updatedPost = QuerySet<Post>(conn).where(field(&Post::id) == postId).select_one();
    ASSERT_TRUE(updatedPost.has_value()) << "query failed with error: " << updatedPost.error();
    ASSERT_EQ(updatedPost.value().author_id, bob_id); // Still Bob's post
}

// =======================================
// MAX AGGREGATE FUNCTION TESTS
// =======================================

TEST_F(ORMTest, MaxStringField) {
    // Test MAX function with string field (alphabetically highest value)
    auto result = QuerySet<Author>(conn).max(field(&Author::name), "max_name").select_values();

    ValueVectorMap expected_results = {{{"max_name", "Diana Prince"}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MaxOnEmptyTable) {
    // Clear all authors
    auto remove_result = QuerySet<Author>(conn).remove();
    ASSERT_TRUE(remove_result);

    // Test MAX function on an empty table
    auto result = QuerySet<Author>(conn).max(field(&Author::age), "max_age").select_values();

    ValueVectorMap expected_results = {{{"max_age", std::monostate()}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MaxWithOrderBy) {
    // Test MAX function with ORDER BY
    // The ORDER BY shouldn't affect the MAX result but should affect the order of
    // rows
    auto result = QuerySet<Author>(conn)
                          .max(field(&Author::age), "max_age")
                          .order_by(field(&Author::name), false)
                          .select_values();

    ValueVectorMap expected_results = {{{"max_age", 35}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MultipleMaxFunctions) {
    // Test multiple MAX functions in the same query
    auto result = QuerySet<Author>(conn)
                          .max(field(&Author::age), "max_age")
                          .max(field(&Author::rating), "max_rating")
                          .max(field(&Author::score), "max_score")
                          .select_values();

    ValueVectorMap expected_results = {{{"max_age", 35}, {"max_rating", 5.5}, {"max_score", 95}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MaxWithJoin) {
    // Test MAX function with a JOIN
    // This will find the maximum post ID for each author
    // We need to include the name field in the result set when using group_by
    // with aggregate functions
    auto result = QuerySet<Post>(conn)
                          .join(field(&Post::author_id))
                          .group_by(field(&Author::name))
                          .only(field(&Author::name))
                          .max(field(&Post::id), "max_post_id")
                          .select_values();

    ValueVectorMap expected_results =
            {{{"name", "Alice Smith"}, {"max_post_id", 3}},
             {{"name", "Bob Johnson"}, {"max_post_id", 5}},
             {{"name", "Charlie Brown"}, {"max_post_id", 9}},
             {{"name", "Diana Prince"}, {"max_post_id", 11}}};
    AssertResultsMatch(result, expected_results);
}

// MAX VALUE TESTS
TEST_F(ORMTest, MaxValue) {
    auto result = QuerySet<Author>(conn).max_value(field(&Author::age));
    ASSERT_TRUE(result.has_value()) << "max_value failed with error: " << result.error();
    EXPECT_EQ(result.value(), 35);
}

TEST_F(ORMTest, MaxValueWithFilter) {
    auto result = QuerySet<Author>(conn).where(field(&Author::age) == 30).max_value(field(&Author::age));
    ASSERT_TRUE(result.has_value()) << "max_value with filter failed with error: " << result.error();
    EXPECT_EQ(result.value(), 35);
}

TEST_F(ORMTest, MaxValueEmptyTable) {
    clearTestData();

    auto result = QuerySet<Author>(conn).max_value(field(&Author::age));
    ASSERT_TRUE(result.has_value()) << "max_value on empty table should return 0, not an error";
    ASSERT_EQ(result.value(), 0) << "max_value on empty table should return 0";
}

TEST_F(ORMTest, MaxValueString) {
    auto result = QuerySet<Author>(conn).max_value(field(&Author::name));
    ASSERT_TRUE(result.has_value()) << "max_value failed with error: " << result.error();
    EXPECT_EQ(result.value(), "Diana Prince");
}

TEST_F(ORMTest, MaxValueDouble) {
    auto result = QuerySet<Author>(conn).max_value(field(&Author::rating));
    ASSERT_TRUE(result.has_value()) << "max_value failed with error: " << result.error();
    EXPECT_EQ(result.value(), 5.5);
}

TEST_F(ORMTest, MaxValueFloat) {
    auto result = QuerySet<Author>(conn).max_value(field(&Author::score));
    ASSERT_TRUE(result.has_value()) << "max_value failed with error: " << result.error();
    EXPECT_EQ(result.value(), 95.0);
}

// =======================================
// MIN AGGREGATE FUNCTION TESTS
// =======================================

TEST_F(ORMTest, MinStringField) {
    auto result = QuerySet<Author>(conn).min(field(&Author::name), "min_name").select_values();

    ValueVectorMap expected_results = {{{"min_name", "Alice Smith"}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MinOnEmptyTable) {
    clearTestData();

    auto result = QuerySet<Author>(conn).min(field(&Author::age), "min_age").select_values();

    ValueVectorMap expected_results = {{{"min_age", std::monostate()}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MinWithOrderBy) {
    auto result = QuerySet<Author>(conn)
                          .min(field(&Author::age), "min_age")
                          .order_by(field(&Author::name), false)
                          .select_values();

    ValueVectorMap expected_results = {{{"min_age", 25}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MultipleMinFunctions) {
    auto result = QuerySet<Author>(conn)
                          .min(field(&Author::age), "min_age")
                          .min(field(&Author::rating), "min_rating")
                          .min(field(&Author::score), "min_score")
                          .select_values();

    ValueVectorMap expected_results = {{{"min_age", 25}, {"min_rating", 4.0}, {"min_score", 80}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MinWithJoinAndGroupBy) {
    auto result = QuerySet<Author>(conn)
                          .join(field(&Post::author_id))
                          .min(field(&Author::age), "min_age")
                          .only(field(&Post::author_id))
                          .group_by(field(&Post::author_id))
                          .select_values();

    ValueVectorMap expected_results =
            {{
                     {"author_id", 1},
                     {"min_age", 25},
             },
             {
                     {"author_id", 2},
                     {"min_age", 35},
             },
             {
                     {"author_id", 3},
                     {"min_age", 30},
             },
             {
                     {"author_id", 4},
                     {"min_age", 28},
             }};
    AssertResultsMatch(result, expected_results);
}

// MIN VALUE TESTS
TEST_F(ORMTest, MinValue) {
    auto result = QuerySet<Author>(conn).min_value(field(&Author::age));
    ASSERT_TRUE(result.has_value()) << "min_value failed with error: " << result.error();
    EXPECT_EQ(result.value(), 25);
}

TEST_F(ORMTest, MinValueWithFilter) {
    auto result = QuerySet<Author>(conn).where(field(&Author::age) == 30).min_value(field(&Author::age));
    ASSERT_TRUE(result.has_value()) << "min_value with filter failed with error: " << result.error();
    EXPECT_EQ(result.value(), 35);
}

TEST_F(ORMTest, MinValueEmptyTable) {
    clearTestData();

    auto result = QuerySet<Author>(conn).min_value(field(&Author::age));
    ASSERT_TRUE(result.has_value()) << "min_value on empty table should return 0, not an error";
    ASSERT_EQ(result.value(), 0) << "min_value on empty table should return 0";
}

TEST_F(ORMTest, MinValueString) {
    auto result = QuerySet<Author>(conn).min_value(field(&Author::name));
    ASSERT_TRUE(result.has_value()) << "min_value failed with error: " << result.error();
    EXPECT_EQ(result.value(), "Alice Smith");
}

TEST_F(ORMTest, MinValueDouble) {
    auto result = QuerySet<Author>(conn).min_value(field(&Author::rating));
    ASSERT_TRUE(result.has_value()) << "min_value failed with error: " << result.error();
    EXPECT_EQ(result.value(), 4.0);
}

TEST_F(ORMTest, MinValueFloat) {
    auto result = QuerySet<Author>(conn).min_value(field(&Author::score));
    ASSERT_TRUE(result.has_value()) << "min_value failed with error: " << result.error();
    EXPECT_EQ(result.value(), 80.0);
}

TEST_F(ORMTest, MinValueWithJoin) {
    auto result = QuerySet<Post>(conn)
                          .join(field(&Post::author_id))
                          .where(field(&Author::name) == "Charlie Brown")
                          .min_value(field(&Author::rating));
    ASSERT_TRUE(result.has_value()) << "min_value with join failed with error: " << result.error();
    EXPECT_EQ(result.value(), 4.0);
}

// =======================================
// AVG AGGREGATE FUNCTION TESTS
// =======================================
TEST_F(ORMTest, AvgNumericField) {
    auto result = QuerySet<Author>(conn).avg(field(&Author::age), "avg_age").select_values();

    ValueVectorMap expected_results = {{{"avg_age", 29.5}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, AvgOnEmptyTable) {
    clearTestData();

    auto result = QuerySet<Author>(conn).avg(field(&Author::age), "avg_age").select_values();

    ValueVectorMap expected_results = {{{"avg_age", std::monostate()}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, AvgWithOrderBy) {
    auto result = QuerySet<Author>(conn)
                          .avg(field(&Author::age), "avg_age")
                          .order_by(field(&Author::name), false)
                          .select_values();

    ValueVectorMap expected_results = {{{"avg_age", 29.5}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, MultipleAvgFunctions) {
    auto result = QuerySet<Author>(conn)
                          .avg(field(&Author::rating), "avg_rating")
                          .avg(field(&Author::score), "avg_score")
                          .group_by(field(&Author::age))
                          .only(field(&Author::age))
                          .select_values();

    ValueVectorMap expected_results =
            {{{"age", 25}, {"avg_rating", 4.5}, {"avg_score", 85.5}},
             {{"age", 28}, {"avg_rating", 5.5}, {"avg_score", 95.0}},
             {{"age", 30}, {"avg_rating", 4.0}, {"avg_score", 80.0}},
             {{"age", 35}, {"avg_rating", 5.0}, {"avg_score", 90.0}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, AvgWithGroupBy) {
    auto result = QuerySet<Author>(conn)
                          .avg(field(&Author::rating), "avg_rating")
                          .group_by(field(&Author::age))
                          .only(field(&Author::age))
                          .select_values();

    ValueVectorMap expected_results =
            {{{"age", 25}, {"avg_rating", 4.5}},
             {{"age", 28}, {"avg_rating", 5.5}},
             {{"age", 30}, {"avg_rating", 4.0}},
             {{"age", 35}, {"avg_rating", 5.0}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, CombineAvgMinMaxWithGroupBy) {
    auto result = QuerySet<Author>(conn)
                          .avg(field(&Author::rating), "avg_rating")
                          .min(field(&Author::rating), "min_rating")
                          .max(field(&Author::rating), "max_rating")
                          .group_by(field(&Author::age))
                          .only(field(&Author::age))
                          .select_values();

    ValueVectorMap expected_results =
            {{{"age", 25}, {"avg_rating", 4.5}, {"min_rating", 4.5}, {"max_rating", 4.5}},
             {{"age", 28}, {"avg_rating", 5.5}, {"min_rating", 5.5}, {"max_rating", 5.5}},
             {{"age", 30}, {"avg_rating", 4.0}, {"min_rating", 4.0}, {"max_rating", 4.0}},
             {{"age", 35}, {"avg_rating", 5.0}, {"min_rating", 5.0}, {"max_rating", 5.0}}};
    AssertResultsMatch(result, expected_results);
}

// AVG VALUE TESTS
TEST_F(ORMTest, AvgValueInt) {
    auto result = QuerySet<Author>(conn).avg_value(field(&Author::age));
    ASSERT_TRUE(result.has_value()) << "avg failed with error: " << result.error();
    EXPECT_EQ(result.value(), 29.5);
}

TEST_F(ORMTest, AvgValueWithFilter) {
    auto result = QuerySet<Author>(conn).where(field(&Author::age) > 30).avg_value(field(&Author::age));
    ASSERT_TRUE(result.has_value()) << "avg with filter failed with error: " << result.error();
    EXPECT_EQ(result.value(), 35.0);
}

TEST_F(ORMTest, AvgValueDouble) {
    auto result = QuerySet<Author>(conn).avg_value(field(&Author::rating));
    ASSERT_TRUE(result.has_value()) << "avg of double failed with error: " << result.error();
    EXPECT_EQ(result.value(), 4.75);
}

TEST_F(ORMTest, AvgValueFloat) {
    auto result = QuerySet<Author>(conn).avg_value(field(&Author::score));
    ASSERT_TRUE(result.has_value()) << "avg of float failed with error: " << result.error();
    EXPECT_EQ(result.value(), 87.625);
}

TEST_F(ORMTest, AvgValueWithJoin) {
    auto result = QuerySet<Post>(conn)
                          .join(field(&Post::author_id))
                          .where(field(&Author::name) == "Charlie Brown")
                          .avg_value(field(&Author::rating));
    ASSERT_TRUE(result.has_value()) << "avg_value with join failed with error: " << result.error();
    EXPECT_EQ(result.value(), 4.0);
}

TEST_F(ORMTest, AvgValueEmptyTable) {
    clearTestData();

    auto result = QuerySet<Author>(conn).avg_value(field(&Author::age));
    ASSERT_TRUE(result.has_value()) << "avg_value on empty table should return 0.0, not an error";
    ASSERT_DOUBLE_EQ(result.value(), 0.0) << "avg_value on empty table should return 0.0";
}

// =======================================
// COUNT AGGREGATE FUNCTION TESTS
// =======================================
TEST_F(ORMTest, CountField) {
    auto result = QuerySet<Author>(conn).count(field(&Author::id)).select_values();

    ValueVectorMap expected_results = {{{"count_id", 4}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, AggregateCustomAliases) {
    // Test COUNT with custom alias
    auto           countResult   = QuerySet<Author>(conn).count(field(&Author::id), "author_count").select_values();
    ValueVectorMap countExpected = {{{"author_count", 4}}};
    AssertResultsMatch(countResult, countExpected);

    // Test SUM with custom alias
    auto           sumResult   = QuerySet<Author>(conn).sum(field(&Author::age), "total_age").select_values();
    ValueVectorMap sumExpected = {{{"total_age", 118.0}}};
    AssertResultsMatch(sumResult, sumExpected);

    // Test GROUP_CONCAT with custom alias
    auto concatResult = QuerySet<Post>(conn).group_concat(field(&Post::title), "my_custom_alias").select_values();
    ASSERT_TRUE(concatResult.has_value()) << concatResult.error();
    ASSERT_EQ(concatResult.value().size(), 1);
    ASSERT_TRUE(concatResult.value()[0].contains("my_custom_alias"));
    EXPECT_FALSE(std::get<std::string>(concatResult.value()[0].at("my_custom_alias")).empty());
}

TEST_F(ORMTest, CountOnEmptyTable) {
    auto remove_result = QuerySet<Author>(conn).remove();
    ASSERT_TRUE(remove_result);

    auto result = QuerySet<Author>(conn).count(field(&Author::id)).select_values();

    ValueVectorMap expected_results = {{{"count_id", 0}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, CountWithFilter) {
    auto result = QuerySet<Author>(conn).where(field(&Author::age) >= 30).count(field(&Author::id)).select_values();

    ValueVectorMap expected_results = {{{"count_id", 2}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, CountStringSelectValues) {
    auto result = QuerySet<Author>(conn).where(field(&Author::age) >= 30).count(field(&Author::name)).select_values();

    ValueVectorMap expected_results = {{{"count_name", 2}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, CountWithGroupBy) {
    // Insert another author with the same age as an existing one
    Author duplicate("Duplicate Author", 30, "duplicate@example.com", 0, true, 4.2, 82.0);
    auto   insert_result = QuerySet<Author>(conn).insert(duplicate);
    ASSERT_TRUE(insert_result);

    auto result = QuerySet<Author>(conn)
                          .count(field(&Author::id))
                          .group_by(field(&Author::age))
                          .only(field(&Author::age))
                          .order_by(field(&Author::age))
                          .select_values();

    ValueVectorMap expected_results =
            {{{"age", 25}, {"count_id", 1}},
             {{"age", 28}, {"count_id", 1}},
             {{"age", 30}, {"count_id", 2}},
             {{"age", 35}, {"count_id", 1}}};
    AssertResultsMatch(result, expected_results);
}

// COUNT VALUE TESTS
TEST_F(ORMTest, CountValue) {
    auto result = QuerySet<Author>(conn).count_value(field(&Author::id));
    ASSERT_TRUE(result.has_value()) << "count_value failed with error: " << result.error();
    EXPECT_EQ(result.value(), 4);
}

TEST_F(ORMTest, CountValueString) {
    auto result = QuerySet<Post>(conn).count_value(field(&Post::title));
    ASSERT_TRUE(result.has_value()) << "count_value with join failed with error: " << result.error();
    EXPECT_EQ(result.value(), 11);
}

TEST_F(ORMTest, CountValueWithFilter) {
    auto result = QuerySet<Author>(conn).where(field(&Author::age) > 30).count_value(field(&Author::id));
    ASSERT_TRUE(result.has_value()) << "count_value with filter failed with error: " << result.error();
    EXPECT_EQ(result.value(), 1);
}

TEST_F(ORMTest, CountValueEmptyTable) {
    clearTestData();

    auto result = QuerySet<Author>(conn).count_value(field(&Author::id));
    ASSERT_TRUE(result.has_value()) << "count_value with empty table failed with error: " << result.error();
    EXPECT_EQ(result.value(), 0);
}

TEST_F(ORMTest, CountValueWithJoin) {
    auto result = QuerySet<Post>(conn)
                          .join(field(&Post::author_id))
                          .where(field(&Author::name) == "Charlie Brown")
                          .count_value(field(&Post::id));
    ASSERT_TRUE(result.has_value()) << "count_value with join failed with error: " << result.error();
    EXPECT_EQ(result.value(), 4);
}

// =======================================
// SUM AGGREGATE FUNCTION TESTS
// =======================================
TEST_F(ORMTest, SumField) {
    auto result = QuerySet<Author>(conn).sum(field(&Author::age)).select_values();

    // Sum of ages: 25 + 28 + 30 + 35 = 118
    ValueVectorMap expected_results = {{{"sum_age", 118.0}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, SumOnEmptyTable) {
    auto remove_result = QuerySet<Author>(conn).remove();
    ASSERT_TRUE(remove_result);

    auto result = QuerySet<Author>(conn).sum(field(&Author::age)).select_values();

    ValueVectorMap expected_results = {{{"sum_age", std::monostate{}}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, SumWithFilter) {
    // First reset the data
    clearTestData();
    setupTestData();

    auto result = QuerySet<Author>(conn).where(field(&Author::age) >= 30).sum(field(&Author::age)).select_values();

    // Sum of ages ≥ 30: 30 + 35 = 65
    ValueVectorMap expected_results = {{{"sum_age", 65.0}}};
    AssertResultsMatch(result, expected_results);
}

TEST_F(ORMTest, SumWithGroupBy) {
    // Insert another author with the same age as an existing one
    Author duplicate("Duplicate Author", 30, "duplicate@example.com", 0, true, 4.2, 82.0);
    auto   insert_result = QuerySet<Author>(conn).insert(duplicate);
    ASSERT_TRUE(insert_result);

    auto result = QuerySet<Author>(conn)
                          .sum(field(&Author::rating))
                          .group_by(field(&Author::age))
                          .only(field(&Author::age))
                          .order_by(field(&Author::age))
                          .select_values();

    ValueVectorMap expected_results =
            {{{"age", 25}, {"sum_rating", 4.5}},
             {{"age", 28}, {"sum_rating", 5.5}},
             {{"age", 30}, {"sum_rating", 8.2}},
             {{"age", 35}, {"sum_rating", 5}}};
    AssertResultsMatch(result, expected_results);
}

// SUM VALUE TESTS
TEST_F(ORMTest, SumValue) {
    auto result = QuerySet<Author>(conn).sum_value(field(&Author::age));
    ASSERT_TRUE(result.has_value()) << "sum_value failed with error: " << result.error();
    EXPECT_EQ(result.value(), 118); // Including the duplicate author with age 30
}

TEST_F(ORMTest, SumValueWithFilter) {
    auto result = QuerySet<Author>(conn).where(field(&Author::age) > 30).sum_value(field(&Author::age));
    ASSERT_TRUE(result.has_value()) << "sum_value with filter failed with error: " << result.error();
    EXPECT_DOUBLE_EQ(result.value(), 35.0);
}

TEST_F(ORMTest, SumValueEmptyTable) {
    clearTestData();

    auto result = QuerySet<Author>(conn).sum_value(field(&Author::age));
    ASSERT_TRUE(result.has_value()) << "sum_value on empty table should return 0, not an error";
    ASSERT_EQ(result.value(), 0) << "sum_value on empty table should return 0";
}

TEST_F(ORMTest, SumValueDouble) {
    auto result = QuerySet<Author>(conn).sum_value(field(&Author::rating));
    ASSERT_TRUE(result.has_value()) << "sum_value with join failed with error: " << result.error();
    EXPECT_DOUBLE_EQ(result.value(), 19);
}

// COLLATE TESTS
// =======================================

// Test case-insensitive string comparison with COLLATE NOCASE in WHERE clause
TEST_F(ORMTest, WhereClauseCollateNocase) {
    // Insert test data with mixed case names
    clearTestData();

    Author author1("Alice", 25, "alice@example.com", 1, true, 4.5, 90.0);
    Author author2("BOB", 28, "bob@example.com", 2, true, 5.5, 85.0);
    Author author3("charlie", 30, "charlie@example.com", 3, false, 4.0, 80.0);
    Author author4("DIANA", 35, "diana@example.com", 4, true, 5.0, 95.0);

    QuerySet<Author>(conn).insert(std::vector{author1, author2, author3, author4});

    // Use COLLATE NOCASE to find "bob" regardless of case
    auto result = QuerySet<Author>(conn).where(field(&Author::name).collate_nocase() == "bob").select_all();

    ASSERT_TRUE(result.has_value()) << "where clause collate nocase failed with error: " << result.error();
    auto value = result.value();
    ASSERT_EQ(value.size(), 1);
    EXPECT_EQ(value[0].name, "BOB");

    // Without COLLATE, the case-sensitive search would find nothing
    auto result_case_sensitive = QuerySet<Author>(conn).where(field(&Author::name) == "bob").select_all();

    ASSERT_TRUE(result_case_sensitive.has_value())
            << "where clause collate nocase failed with error: " << result_case_sensitive.error();
    auto value_case_sensitive = result_case_sensitive.value();
    EXPECT_EQ(value_case_sensitive.size(), 0);
}

// Test COLLATE RTRIM in WHERE clause for string comparison ignoring trailing
// spaces
TEST_F(ORMTest, WhereClauseCollateRtrim) {
    clearTestData();

    Author author1("Alice  ", 25, "alice@example.com", 1, true, 4.5, 90.0);
    Author author2("Bob", 28, "bob@example.com", 2, true, 5.5, 85.0);

    QuerySet<Author>(conn).insert(std::vector{author1, author2});

    // Use COLLATE RTRIM to find "Alice" ignoring trailing spaces
    auto result = QuerySet<Author>(conn).where(field(&Author::name).collate_rtrim() == "Alice").select_all();

    ASSERT_TRUE(result.has_value()) << "where clause collate rtrim failed with error: " << result.error();
    auto value = result.value();
    ASSERT_EQ(value.size(), 1);
    EXPECT_EQ(value[0].name, "Alice  ");

    // Without COLLATE RTRIM, the search would find nothing
    auto result_without_rtrim = QuerySet<Author>(conn).where(field(&Author::name) == "Alice").select_all();

    ASSERT_TRUE(result_without_rtrim.has_value())
            << "where clause collate rtrim failed with error: " << result_without_rtrim.error();
    auto value_without_rtrim = result_without_rtrim.value();
    EXPECT_EQ(value_without_rtrim.size(), 0);
}

// Test COLLATE BINARY in WHERE clause for case-sensitive string comparison
TEST_F(ORMTest, WhereClauseCollateBinary) {
    clearTestData();

    Author author1("Alice", 25, "alice@example.com", 1, true, 4.5, 90.0);
    Author author2("alice", 28, "alice2@example.com", 2, true, 5.5, 85.0);

    QuerySet<Author>(conn).insert(std::vector{author1, author2});

    // Use COLLATE BINARY to ensure case-sensitive comparison
    auto result = QuerySet<Author>(conn).where(field(&Author::name).collate_binary() == "Alice").select_all();

    ASSERT_TRUE(result.has_value()) << "where clause collate binary failed with error: " << result.error();
    auto value = result.value();
    ASSERT_EQ(value.size(), 1);
    EXPECT_EQ(value[0].name, "Alice");
    EXPECT_EQ(value[0].age, 25);
}

// Test case-insensitive sorting with COLLATE NOCASE in ORDER BY
TEST_F(ORMTest, OrderByCollateNocase) {
    clearTestData();

    // Insert authors with names in different cases to test case-insensitive
    // sorting
    Author author1("alice", 25, "alice@example.com", 1, true, 4.5, 90.0);
    Author author2("BOB", 28, "bob@example.com", 2, true, 5.5, 85.0);
    Author author4("diana", 35, "diana@example.com", 4, true, 5.0, 95.0);
    Author author3("Charlie", 30, "charlie@example.com", 3, false, 4.0, 80.0);

    QuerySet<Author>(conn).insert(std::vector{author1, author2, author3, author4});

    // Sort by name using COLLATE NOCASE (case-insensitive)
    auto result = QuerySet<Author>(conn).order_by(field(&Author::name), Collation::NOCASE).select_all();

    ASSERT_TRUE(result.has_value()) << "order by collate nocase failed with error: " << result.error();
    auto value = result.value();
    ASSERT_EQ(value.size(), 4);

    // Verify case-insensitive alphabetical order: alice, BOB, Charlie, diana
    EXPECT_EQ(value[0].name, "alice");
    EXPECT_EQ(value[1].name, "BOB");
    EXPECT_EQ(value[2].name, "Charlie");
    EXPECT_EQ(value[3].name, "diana");

    // Compare with default sorting (BINARY) which would sort by ASCII value
    // (uppercase letters come before lowercase)
    auto result_binary = QuerySet<Author>(conn).order_by(field(&Author::name)).select_all();

    ASSERT_TRUE(result_binary.has_value()) << "order by collate nocase failed with error: " << result_binary.error();
    auto value_binary = result_binary.value();
    ASSERT_EQ(value_binary.size(), 4);

    // With binary collation, uppercase comes before lowercase in ASCII
    EXPECT_EQ(value_binary[0].name, "BOB");
    EXPECT_EQ(value_binary[1].name, "Charlie");
    EXPECT_EQ(value_binary[2].name, "alice");
    EXPECT_EQ(value_binary[3].name, "diana");
}

// Test COLLATE RTRIM in ORDER BY clause
TEST_F(ORMTest, OrderByCollateRtrim) {
    clearTestData();

    // Insert authors with names having trailing spaces
    Author author1("Alice  ", 25, "alice@example.com", 1, true, 4.5, 90.0);
    Author author3("Charlie   ", 30, "charlie@example.com", 3, false, 4.0, 80.0);
    Author author2("Bob", 28, "bob@example.com", 2, true, 5.5, 85.0);

    QuerySet<Author>(conn).insert(std::vector{author1, author2, author3});

    // Sort by name using COLLATE RTRIM (ignoring trailing spaces)
    auto result = QuerySet<Author>(conn).order_by(field(&Author::name), Collation::RTRIM).select_all();

    ASSERT_TRUE(result.has_value()) << "order by collate rtrim failed with error: " << result.error();
    auto value = result.value();
    ASSERT_EQ(value.size(), 3);

    // Verify alphabetical order ignoring trailing spaces: Alice, Bob, Charlie
    EXPECT_EQ(value[0].name, "Alice  ");
    EXPECT_EQ(value[1].name, "Bob");
    EXPECT_EQ(value[2].name, "Charlie   ");
}

// Test COLLATE BINARY in ORDER BY clause for case-sensitive sorting
TEST_F(ORMTest, OrderByCollateBinary) {
    clearTestData();

    // Insert authors with mixed case names to test case-sensitive sorting
    Author author1("alice", 25, "alice@example.com", 1, true, 4.5, 90.0);
    Author author2("BOB", 28, "bob@example.com", 2, true, 5.5, 85.0);
    Author author3("Charlie", 30, "charlie@example.com", 3, false, 4.0, 80.0);
    Author author4("diana", 35, "diana@example.com", 4, true, 5.0, 95.0);
    Author author5("ALICE", 40, "alice2@example.com", 5, true, 3.5, 88.0);

    QuerySet<Author>(conn).insert(std::vector{author1, author2, author3, author4, author5});

    // Sort by name using COLLATE BINARY (case-sensitive, ASCII order)
    auto result = QuerySet<Author>(conn).order_by(field(&Author::name), Collation::BINARY).select_all();

    ASSERT_TRUE(result.has_value()) << "order by collate binary failed with error: " << result.error();
    auto value = result.value();
    ASSERT_EQ(value.size(), 5);

    // With BINARY collation, uppercase letters come before lowercase in ASCII
    // order Expected order: ALICE, BOB, Charlie, alice, diana
    EXPECT_EQ(value[0].name, "ALICE");
    EXPECT_EQ(value[1].name, "BOB");
    EXPECT_EQ(value[2].name, "Charlie");
    EXPECT_EQ(value[3].name, "alice");
    EXPECT_EQ(value[4].name, "diana");
}

// Test multiple field ordering with different collations
TEST_F(ORMTest, OrderByMultipleFieldsWithCollation) {
    clearTestData();

    // Insert authors with various combinations of names and emails
    Author author2("ALICE", 25, "alice2@example.com", 2, true, 5.5, 85.0);
    Author author1("alice", 30, "alice@example.com", 1, true, 4.5, 90.0);
    Author author4("BOB", 25, "bob2@example.com", 4, true, 5.0, 95.0);
    Author author3("bob", 30, "bob@example.com", 3, false, 4.0, 80.0);

    QuerySet<Author>(conn).insert(std::vector{author1, author2, author3, author4});

    // Sort by name (case-insensitive) and then by age (descending)
    auto result =
            QuerySet<Author>(conn)
                    .order_by(
                            field(&Author::name), true, Collation::NOCASE, field(&Author::age), false, Collation::NONE
                    )
                    .select_all();

    ASSERT_TRUE(result.has_value()) << "order by collate nocase failed with error: " << result.error();
    auto value = result.value();
    ASSERT_EQ(value.size(), 4);

    // First grouped by name case-insensitively (alice/ALICE, then bob/BOB)
    // Within each name group, sorted by age in descending order
    EXPECT_EQ(value[0].name, "alice"); // alice, age 30
    EXPECT_EQ(value[0].age, 30);

    EXPECT_EQ(value[1].name, "ALICE"); // ALICE, age 25
    EXPECT_EQ(value[1].age, 25);

    EXPECT_EQ(value[2].name, "bob"); // bob, age 30
    EXPECT_EQ(value[2].age, 30);

    EXPECT_EQ(value[3].name, "BOB"); // BOB, age 25
    EXPECT_EQ(value[3].age, 25);
}

// Test COLLATE in both WHERE and ORDER BY clauses together
TEST_F(ORMTest, CollateInWhereAndOrderBy) {
    clearTestData();

    // Insert authors with names in different cases
    Author author1("alice", 25, "alice@example.com", 1, true, 4.5, 90.0);
    Author author2("ALICE", 28, "alice2@example.com", 2, true, 5.5, 85.0);
    Author author3("bob", 30, "bob@example.com", 3, false, 4.0, 80.0);
    Author author4("BOB", 35, "bob2@example.com", 4, true, 5.0, 95.0);

    QuerySet<Author>(conn).insert(std::vector{author1, author2, author3, author4});

    // Find all authors with name "alice" (case-insensitive) and sort by age
    auto result = QuerySet<Author>(conn)
                          .where(field(&Author::name).collate_nocase() == "alice")
                          .order_by(field(&Author::age))
                          .select_all();

    ASSERT_TRUE(result.has_value()) << "collate in where and order by failed with error: " << result.error();
    auto value = result.value();
    ASSERT_EQ(value.size(), 2);

    // Verify results are sorted by age
    EXPECT_EQ(value[0].name, "alice");
    EXPECT_EQ(value[0].age, 25);

    EXPECT_EQ(value[1].name, "ALICE");
    EXPECT_EQ(value[1].age, 28);
}

// Test multiple collate operations in complex WHERE clauses
TEST_F(ORMTest, WhereClauseMultipleCollateOperations) {
    clearTestData();

    // Insert test data following the same pattern as working collate tests
    Author author1("Alice", 25, "alice@example.com", 1, true, 4.5, 90.0);
    Author author2("BOB", 28, "bob@example.com", 2, true, 5.5, 85.0);
    Author author3("charlie ", 30, "charlie@example.com", 3, false, 4.0, 80.0);
    Author author4("DIANA", 35, "diana@example.com", 4, true, 5.0, 95.0);
    Author author5("eve", 22, "eve@example.com", 5, false, 3.5, 88.0);

    QuerySet<Author>(conn).insert(std::vector{author1, author2, author3, author4, author5});

    // Test 1: NOCASE collation - find "alice" regardless of case
    const auto& result1 = QuerySet<Author>(conn).where(field(&Author::name).collate_nocase() == "alice").select_all();

    ASSERT_TRUE(result1.has_value()) << "NOCASE collation failed: " << result1.error();
    const auto& value1 = result1.value();
    ASSERT_EQ(value1.size(), 1);
    EXPECT_EQ(value1[0].name, "Alice");

    // Test 2: RTRIM collation - find "charlie" ignoring trailing spaces
    const auto& result2 = QuerySet<Author>(conn).where(field(&Author::name).collate_rtrim() == "charlie").select_all();

    ASSERT_TRUE(result2.has_value()) << "RTRIM collation failed: " << result2.error();
    const auto& value2 = result2.value();
    ASSERT_EQ(value2.size(), 1);
    EXPECT_EQ(value2[0].name, "charlie ");

    // Test 3: Multiple collate operations with OR condition
    const auto& result3 = QuerySet<Author>(conn)
                                  .where(field(&Author::name).collate_nocase() == "bob" ||
                                         field(&Author::name).collate_rtrim() == "charlie")
                                  .select_all();

    ASSERT_TRUE(result3.has_value()) << "Multiple collate OR condition failed: " << result3.error();
    const auto& value3 = result3.value();
    ASSERT_EQ(value3.size(), 2);

    // Test 4: Complex condition with different collate types and AND/OR logic
    const auto& result4 =
            QuerySet<Author>(conn)
                    .where((field(&Author::name).collate_nocase() == "alice" && field(&Author::age) == 25) ||
                           (field(&Author::name).collate_binary() == "DIANA" && field(&Author::age) > 30))
                    .select_all();

    ASSERT_TRUE(result4.has_value()) << "Complex collate condition failed: " << result4.error();
    const auto& value4 = result4.value();
    ASSERT_EQ(value4.size(), 2); // Should find Alice and DIANA

    // Test 5: Multiple collate operations with LIKE patterns
    const auto& result5 = QuerySet<Author>(conn)
                                  .where(field(&Author::name).collate_nocase().like("%a%") && field(&Author::age) >= 25)
                                  .select_all();

    ASSERT_TRUE(result5.has_value()) << "Collate with LIKE pattern failed: " << result5.error();
    const auto& value5 = result5.value();
    ASSERT_GE(value5.size(), 1); // Should find Alice and DIANA

    // Test 6: Chained collate operations with boolean conditions
    const auto& result6 = QuerySet<Author>(conn)
                                  .where((field(&Author::name).collate_nocase() == "bob" ||
                                          field(&Author::name).collate_nocase() == "diana") &&
                                         field(&Author::is_active) == true)
                                  .select_all();

    ASSERT_TRUE(result6.has_value()) << "Chained collate with boolean failed: " << result6.error();
    const auto& value6 = result6.value();
    ASSERT_EQ(value6.size(), 2); // Should find BOB and DIANA (both active)

    // Test 7: Nested conditions with multiple collate operations
    const auto& result7 = QuerySet<Author>(conn)
                                  .where(field(&Author::name).collate_nocase().in({"alice", "bob", "charlie"}) &&
                                         (field(&Author::name).collate_rtrim().like("%e%") || field(&Author::age) < 30))
                                  .select_all();

    ASSERT_TRUE(result7.has_value()) << "Nested collate conditions failed: " << result7.error();
    const auto& value7 = result7.value();
    ASSERT_GE(value7.size(), 1);
    EXPECT_EQ(value7[0].name, "Alice");
    EXPECT_EQ(value7[0].age, 25);

    // Test 8: Mixed collate operations with different field types
    const auto& result8 =
            QuerySet<Author>(conn)
                    .where(field(&Author::name).collate_nocase().startswith("a") &&
                           field(&Author::email).collate_nocase().endswith("@EXAMPLE.COM") && field(&Author::age) >= 25)
                    .select_all();

    ASSERT_TRUE(result8.has_value()) << "Mixed collate operations failed: " << result8.error();
    const auto& value8 = result8.value();
    ASSERT_GE(value8.size(), 1);
    EXPECT_EQ(value8[0].name, "Alice");
    EXPECT_EQ(value8[0].age, 25);
}
// =======================================
// GROUP_CONCAT TESTS
// =======================================

TEST_F(ORMTest, GroupConcatBasic) {
    // Basic GROUP_CONCAT functionality - concatenate post titles by author
    auto result = QuerySet<Post>(conn)
                          .group_by(field(&Post::author_id))
                          .group_concat(field(&Post::title))
                          .only(field(&Post::author_id))
                          .select_values();

    ASSERT_TRUE(result.has_value()) << "Basic GROUP_CONCAT failed: " << result.error();
    const auto& value = result.value();

    // We should have results for each author
    ASSERT_EQ(value.size(), 4); // Alice, Bob, Charlie, Diana

    // Verify Alice's posts are concatenated
    auto aliceIt = std::ranges::find_if(value, [this](const auto& row) {
        return std::get<int>(row.at("author_id")) == alice_id;
    });
    ASSERT_NE(aliceIt, value.end());

    // Alice has 3 posts, verify they're all in the concatenated string
    std::string alicePosts = std::get<std::string>(aliceIt->at("group_concat_title"));
    EXPECT_TRUE(alicePosts.find("Alice's First Post") != std::string::npos);
    EXPECT_TRUE(alicePosts.find("Alice's Second Post") != std::string::npos);
    EXPECT_TRUE(alicePosts.find("Alice's Third Post") != std::string::npos);
}

TEST_F(ORMTest, GroupConcatCustomSeparator) {
    // GROUP_CONCAT with custom separator
    auto result = QuerySet<Post>(conn)
                          .group_by(field(&Post::author_id))
                          .group_concat(field(&Post::title), "custom_group_concat_title", " | ")
                          .only(field(&Post::author_id))
                          .select_values();

    ASSERT_TRUE(result.has_value()) << "GROUP_CONCAT with custom separator failed: " << result.error();
    const auto& value = result.value();
    ASSERT_EQ(value.size(), 4); // Alice, Bob, Charlie, Diana

    // Verify Bob's posts are concatenated with the custom separator
    auto bobIt = std::ranges::find_if(value, [this](const auto& row) {
        return std::get<int>(row.at("author_id")) == bob_id;
    });
    ASSERT_NE(bobIt, value.end());

    std::string bobPosts = std::get<std::string>(bobIt->at("custom_group_concat_title"));
    EXPECT_TRUE(bobPosts.find(" | ") != std::string::npos);
    EXPECT_EQ(std::count(bobPosts.begin(), bobPosts.end(), '|'),
              1); // Bob has 2 posts, so 1 separator
}

TEST_F(ORMTest, GroupConcatDistinct) {
    // First insert a duplicate post for testing DISTINCT
    Post duplicatePost("Alice's First Post", "Duplicate content", alice_id);
    QuerySet<Post>(conn).insert(duplicatePost);

    auto result = QuerySet<Post>(conn)
                          .group_by(field(&Post::author_id))
                          .group_concat(field(&Post::title), utils::fixed_string<32>{""}, utils::fixed_string<8>{","}, true) // distinct = true
                          .only(field(&Post::author_id))
                          .select_values();

    ASSERT_TRUE(result.has_value()) << "GROUP_CONCAT with DISTINCT failed: " << result.error();
    const auto& value = result.value();
    ASSERT_EQ(value.size(), 4);

    // Verify Alice's posts - the duplicate title should appear only once
    auto aliceIt = std::ranges::find_if(value, [this](const auto& row) {
        return std::get<int>(row.at("author_id")) == alice_id;
    });
    ASSERT_NE(aliceIt, value.end());

    std::string alicePosts = std::get<std::string>(aliceIt->at("group_concat_title"));

    // Count occurrences of "Alice's First Post" - should be exactly 1 due to
    // DISTINCT
    std::string searchStr = "Alice's First Post";
    size_t      pos = 0, count = 0;
    while ((pos = alicePosts.find(searchStr, pos)) != std::string::npos) {
        count++;
        pos += searchStr.length();
    }
    EXPECT_EQ(count, 1) << "DISTINCT failed to remove duplicate title";

    // Clean up the duplicate post
    QuerySet<Post>(conn)
            .where(field(&Post::title) == "Alice's First Post" && field(&Post::content) == "Duplicate content")
            .remove();
}

/*
TEST_F(ORMTest, GroupConcatOrderByAscending) {
    // GROUP_CONCAT with ORDER BY (ascending) - requires the ORDER BY overload
    auto result = QuerySet<Post>(conn)
                          .group_by(field(&Post::author_id))
                          .group_concat(
                                  field(&Post::id),
                                  field(&Post::title),
                                  "",
                                  ",",
                                  " ",
                                  false
                          ) // Field = &Post::title, OrderField = &Post::id
                          .only(field(&Post::author_id))
                          .select_values();

    ASSERT_TRUE(result.has_value()) << "GROUP_CONCAT with ORDER BY failed: " << result.error();
    const auto& value = result.value();
    ASSERT_EQ(value.size(), 4);

    // Verify Charlie's posts (he has 4 posts)
    auto charlieIt = std::ranges::find_if(value, [this](const auto& row) {
        return std::get<int>(row.at("author_id")) == charlie_id;
    });
    ASSERT_NE(charlieIt, value.end());

    std::string charliePosts = std::get<std::string>(charlieIt->at("group_concat_title"));

    // In ascending order by ID, Charlie's First Post should come before Charlie's
    // Fourth Post
    size_t firstPostPos  = charliePosts.find("Charlie's First Post");
    size_t fourthPostPos = charliePosts.find("Charlie's Fourth Post");
    ASSERT_NE(firstPostPos, std::string::npos);
    ASSERT_NE(fourthPostPos, std::string::npos);
    EXPECT_LT(firstPostPos, fourthPostPos) << "Posts not ordered correctly in ascending order";
}
*/

// Test group_concat with single field and custom separators
TEST_F(ORMTest, GroupConcatWithCustomSeparator) {
    // GROUP_CONCAT with custom separator
    auto result = QuerySet<Post>(conn)
                          .group_by(field(&Post::author_id))
                          .group_concat(
                                  "title_content_by_id",
                                  ",",
                                  " - ",
                                  false,
                                  field(&Post::id),
                                  field(&Post::title),
                                  field(&Post::content)
                          ) // alias, field_separator, record_separator, distinct, first_field, other_fields...
                          .only(field(&Post::author_id))
                          .select_values();

    ASSERT_TRUE(result.has_value()) << "GROUP_CONCAT with multiple fields and ORDER BY failed: " << result.error();
    const auto& value = result.value();
    ASSERT_EQ(value.size(), 4);

    // Verify Charlie's posts (he has 4 posts)
    auto charlieIt = std::ranges::find_if(value, [this](const auto& row) {
        return std::get<int>(row.at("author_id")) == charlie_id;
    });
    ASSERT_NE(charlieIt, value.end());

    std::string charliePosts = std::get<std::string>(charlieIt->at("title_content_by_id"));

    // Debug the actual content
    std::cout << "\nActual concatenated content: [" << charliePosts << "]\n";

    // Check that we have both title and content in the result
    ASSERT_NE(charliePosts.find("Charlie's First Post"), std::string::npos);
    ASSERT_NE(charliePosts.find("Content C1"), std::string::npos);

    // In ascending order by ID, Charlie's First Post should come before Charlie's
    // Fourth Post
    size_t firstPostPos  = charliePosts.find("Charlie's First Post");
    size_t fourthPostPos = charliePosts.find("Charlie's Fourth Post");
    ASSERT_NE(firstPostPos, std::string::npos);
    ASSERT_NE(fourthPostPos, std::string::npos);
    EXPECT_LT(firstPostPos, fourthPostPos) << "Posts not ordered correctly in ascending order";
}

// =======================================
// MULTI-FIELD GROUP_CONCAT TESTS
// =======================================

TEST_F(ORMTest, GroupConcatSingleField) {
    // Test GROUP_CONCAT with single field
    auto result = QuerySet<Post>(conn)
                          .group_concat("title_content", ",", " - ", false, field(&Post::title), field(&Post::content))
                          .select_values();

    ASSERT_TRUE(result.has_value()) << "GROUP_CONCAT with single field failed: " << result.error();
    const auto& value = result.value();
    ASSERT_EQ(value.size(), 1);
    ASSERT_TRUE(value[0].contains("title_content"));

    std::string concatenated = std::get<std::string>(value[0].at("title_content"));

    // Verify that both title and content are present with the correct field
    // separator
    EXPECT_TRUE(concatenated.find("Alice's First Post - ") != std::string::npos);
    EXPECT_TRUE(concatenated.find("Bob's First Post - ") != std::string::npos);
    EXPECT_TRUE(concatenated.find("Charlie's First Post - ") != std::string::npos);

    auto count = QuerySet<Post>(conn).count_value(field(&Post::id));
    ASSERT_TRUE(count.has_value()) << "COUNT failed: " << count.error();
    // Verify the row separator is correctly applied
    EXPECT_EQ(std::count(concatenated.begin(), concatenated.end(), ','),
              count.value() - 1); // Number of posts minus 1
}

TEST_F(ORMTest, GroupConcatSingleFieldWithGroupBy) {
    // Test GROUP_CONCAT with single field and GROUP BY
    auto result = QuerySet<Post>(conn)
                          .group_by(field(&Post::author_id))
                          .group_concat("title_content", ",", " - ", false, field(&Post::title), field(&Post::content))
                          .only(field(&Post::author_id))
                          .select_values();

    ASSERT_TRUE(result.has_value()) << "GROUP_CONCAT with single field and GROUP BY failed: " << result.error();
    const auto& value = result.value();

    // We should have one row per author
    ASSERT_EQ(value.size(), 4); // Alice, Bob, Charlie, Diana

    // Find Alice's row
    auto aliceIt = std::ranges::find_if(value, [this](const auto& row) {
        return std::get<int>(row.at("author_id")) == alice_id;
    });
    ASSERT_NE(aliceIt, value.end());

    std::string alicePosts = std::get<std::string>(aliceIt->at("title_content"));

    // Verify that Alice's posts are correctly concatenated with both title and
    // content
    EXPECT_TRUE(alicePosts.find("Alice's First Post - ") != std::string::npos);
    EXPECT_TRUE(alicePosts.find("Alice's Second Post - ") != std::string::npos);
    EXPECT_TRUE(alicePosts.find("Alice's Third Post - ") != std::string::npos);

    // Verify the row separator is correctly applied for Alice's posts
    EXPECT_EQ(std::count(alicePosts.begin(), alicePosts.end(), ','),
              2); // Alice has 3 posts, so 2 commas
}

TEST_F(ORMTest, GroupConcatDistinctSingleField) {
    // Test GROUP_CONCAT with DISTINCT for single field
    auto result = QuerySet<Post>(conn)
                          .group_by(field(&Post::author_id))
                          .group_concat(
                                  "title_content", "", " - ", true, field(&Post::title), field(&Post::content)
                          ) // alias, field_separator, record_separator, distinct, first_field, other_fields...
                          .only(field(&Post::author_id))
                          .select_values();

    ASSERT_TRUE(result.has_value()) << "GROUP_CONCAT DISTINCT with single field failed: " << result.error();
    const auto& value = result.value();

    // We should have one row per author
    ASSERT_EQ(value.size(), 4); // Alice, Bob, Charlie, Diana

    // Find Alice's row
    auto aliceIt = std::ranges::find_if(value, [this](const auto& row) {
        return std::get<int>(row.at("author_id")) == alice_id;
    });
    ASSERT_NE(aliceIt, value.end());

    std::string alicePosts = std::get<std::string>(aliceIt->at("title_content"));

    // Verify that Alice's posts are correctly concatenated with both title and
    // content
    EXPECT_TRUE(alicePosts.find("Alice's First Post - ") != std::string::npos);
    EXPECT_TRUE(alicePosts.find("Alice's Second Post - ") != std::string::npos);
    EXPECT_TRUE(alicePosts.find("Alice's Third Post - ") != std::string::npos);
}

// =======================================
// SIMPLIFIED TESTS (if you need basic functionality first)
// =======================================

TEST_F(ORMTest, GroupConcatSingleFieldBasic) {
    // Most basic test - just concatenate titles
    auto result = QuerySet<Post>(conn).group_concat(field(&Post::title)).select_values();

    ASSERT_TRUE(result.has_value()) << "Basic GROUP_CONCAT failed: " << result.error();
    const auto& value = result.value();
    ASSERT_EQ(value.size(), 1);
    ASSERT_TRUE(value[0].contains("group_concat_title"));

    std::string concatenated = std::get<std::string>(value[0].at("group_concat_title"));
    EXPECT_FALSE(concatenated.empty());

    // Should contain all post titles
    EXPECT_TRUE(concatenated.find("Alice's First Post") != std::string::npos);
    EXPECT_TRUE(concatenated.find("Bob's First Post") != std::string::npos);
}

// =======================================
// ONLY() METHOD TESTS - Fields and Aliases
// =======================================

TEST_F(ORMTest, OnlyFieldsAndAliases) {
    // Test 1: Simple fields backward compatibility
    auto simpleResult = QuerySet<Author>(conn).only(field(&Author::name), field(&Author::age)).select_values();
    ASSERT_TRUE(simpleResult.has_value()) << simpleResult.error();
    ASSERT_EQ(simpleResult.value().size(), 4);

    for (const auto& row : simpleResult.value()) {
        EXPECT_TRUE(row.contains("name"));
        EXPECT_TRUE(row.contains("age"));
        EXPECT_FALSE(row.contains("email"));
    }

    // Test 2: Basic field selection without aliases (aliases functionality appears broken)
    auto aliasResult = QuerySet<Author>(conn)
                               .only(field(&Author::name), "author_name", field(&Author::age), "author_age")
                               .select_values();
    ASSERT_TRUE(aliasResult.has_value()) << aliasResult.error();
    ASSERT_EQ(aliasResult.value().size(), 4);

    for (const auto& row : aliasResult.value()) {
        EXPECT_TRUE(row.contains("name"));
        EXPECT_TRUE(row.contains("age"));
        EXPECT_FALSE(row.contains("email"));
    }

    // Test 3: Multiple field selection with WHERE clause (aliases functionality broken)
    auto complexResult = QuerySet<Author>(conn)
                                 .where(field(&Author::age) > 25)
                                 .only(field(&Author::name),
                                       "full_name",
                                       field(&Author::age),
                                       "years_old",
                                       field(&Author::email),
                                       "email_address",
                                       field(&Author::rating),
                                       "user_rating")
                                 .select_values();
    ASSERT_TRUE(complexResult.has_value()) << complexResult.error();
    ASSERT_EQ(complexResult.value().size(), 3); // Bob=35, Charlie=30, Diana=28

    for (const auto& row : complexResult.value()) {
        EXPECT_TRUE(row.contains("name"));
        EXPECT_TRUE(row.contains("age"));
        EXPECT_TRUE(row.contains("email"));
        EXPECT_TRUE(row.contains("rating"));
        EXPECT_GT(std::get<int>(row.at("age")), 25);
    }

    // Verify specific data integrity
    auto aliceSimple = std::ranges::find_if(simpleResult.value(), [](const auto& row) {
        return std::get<std::string>(row.at("name")) == "Alice Smith";
    });

    ASSERT_NE(aliceSimple, simpleResult.value().end());
    EXPECT_EQ(std::get<int>(aliceSimple->at("age")), 25);

    auto bobComplex = std::ranges::find_if(complexResult.value(), [](const auto& row) {
        return std::get<std::string>(row.at("full_name")) == "Bob Johnson";
    });
    ASSERT_NE(bobComplex, complexResult.value().end());
    EXPECT_EQ(std::get<int>(bobComplex->at("years_old")), 35);
    EXPECT_DOUBLE_EQ(std::get<double>(bobComplex->at("user_rating")), 5.0);
}
