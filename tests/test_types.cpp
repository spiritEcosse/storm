#include <gtest/gtest.h>
#include "test_db_helpers.h"

#include <numbers>

// NOLINTBEGIN(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)

import storm;
import <string>;
import <vector>;
import <expected>;
import <optional>;
import <cstdint>;

using namespace storm;

// ===== TEST MODELS (small structs to avoid compiler constexpr bug) =====

// Integer types model
struct IntTypes {
    [[= storm::meta::FieldAttr::primary]] int id;
    int64_t                                   big_num;
    short                                     small_num;
};

// Floating point types model
struct FloatTypes {
    [[= storm::meta::FieldAttr::primary]] int id;
    double                                    precise;
    float                                     approx;
};

// Boolean and string model
struct MixedTypes {
    [[= storm::meta::FieldAttr::primary]] int id{};
    bool                                      active{};
    std::string                               name;
};

// Optional types model (simplified to avoid compiler constexpr bug)
struct OptTypes {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::optional<int>                        maybe_num;
    std::string                               name; // Use regular string instead of optional to reduce complexity
};

// BLOB types model
struct DataTypes {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::vector<uint8_t>                      binary_data;
    std::string                               label;
};

// ===== INTEGER TYPES TESTS =====

template <typename ConnType> class IntTypesInsertUpdateTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        const auto& conn_str = storm::test::get_connection_string<ConnType>();
        auto        result   = QuerySet<IntTypes, ConnType>::set_default_connection(conn_str);
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<IntTypes, ConnType>::get_default_connection();

        ASSERT_TRUE(
                storm::test::ensure_table<ConnType>(
                        conn,
                        "CREATE TABLE IntTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, big_num INTEGER, "
                        "small_num INTEGER)"
                )
                        .has_value()
        );

        storm::test::begin_test_txn<ConnType>(conn, {"IntTypes"});
    }
    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<IntTypes, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<IntTypes, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<IntTypes, ConnType>::clear_default_connection();
    }
};

TYPED_TEST_SUITE(IntTypesInsertUpdateTest, DatabaseTypes);

TYPED_TEST(IntTypesInsertUpdateTest, InsertSingleIntTypes) {
    QuerySet<IntTypes, TypeParam> qs;
    IntTypes const                obj{.id = 0, .big_num = 9223372036854775807LL, .small_num = 32767};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    ASSERT_EQ(selected.value().size(), 1);
    EXPECT_EQ(selected.value().begin()->big_num, 9223372036854775807LL);
    EXPECT_EQ(selected.value().begin()->small_num, 32767);
}

TYPED_TEST(IntTypesInsertUpdateTest, InsertBatchIntTypes) {
    QuerySet<IntTypes, TypeParam> qs;
    std::vector<IntTypes>         batch = {{0, 100LL, 10}, {0, 200LL, 20}, {0, 300LL, 30}};

    auto result = qs.insert(batch);
    ASSERT_TRUE(result.has_value());

    // Batch insert returns void, verify via SELECT
    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
    EXPECT_EQ(selected.value().begin()->big_num, 100LL);
}

TYPED_TEST(IntTypesInsertUpdateTest, UpdateSingleIntTypes) {
    QuerySet<IntTypes, TypeParam> qs;
    IntTypes const                obj{.id = 0, .big_num = 100LL, .small_num = 10};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    // Verify INSERT worked
    auto check1 = qs.select();
    ASSERT_TRUE(check1.has_value());
    auto it1 = check1.value().begin();
    EXPECT_EQ(it1->big_num, 100LL);
    EXPECT_EQ(it1->small_num, 10);

    // Update with the returned ID
    IntTypes const updated{.id = static_cast<int>(id), .big_num = 999LL, .small_num = 99};
    auto           update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    auto it2 = selected.value().begin();
    EXPECT_EQ(it2->big_num, 999LL);
    EXPECT_EQ(it2->small_num, 99);
}

TYPED_TEST(IntTypesInsertUpdateTest, UpdateBatchIntTypes) {
    QuerySet<IntTypes, TypeParam> qs;
    std::vector<IntTypes>         batch = {{1, 100LL, 10}, {2, 200LL, 20}, {3, 300LL, 30}};

    auto insert_result = qs.insert(batch);
    ASSERT_TRUE(insert_result.has_value());

    // Update all
    std::vector<IntTypes> updates       = {{1, 111LL, 11}, {2, 222LL, 22}, {3, 333LL, 33}};
    auto                  update_result = qs.update(std::span<const IntTypes>(updates));
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    auto it = selected.value().begin();
    EXPECT_EQ(it->big_num, 111LL);
    ++it;
    EXPECT_EQ(it->big_num, 222LL);
    ++it;
    EXPECT_EQ(it->big_num, 333LL);
}

// ===== FLOATING POINT TYPES TESTS =====

template <typename ConnType> class FloatTypesInsertUpdateTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        const auto& conn_str = storm::test::get_connection_string<ConnType>();
        auto        result   = QuerySet<FloatTypes, ConnType>::set_default_connection(conn_str);
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<FloatTypes, ConnType>::get_default_connection();

        ASSERT_TRUE(
                storm::test::ensure_table<ConnType>(
                        conn,
                        "CREATE TABLE FloatTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, precise REAL, approx REAL)"
                )
                        .has_value()
        );

        storm::test::begin_test_txn<ConnType>(conn, {"FloatTypes"});
    }
    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<FloatTypes, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<FloatTypes, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<FloatTypes, ConnType>::clear_default_connection();
    }
};

TYPED_TEST_SUITE(FloatTypesInsertUpdateTest, DatabaseTypes);

TYPED_TEST(FloatTypesInsertUpdateTest, InsertSingleFloatTypes) {
    QuerySet<FloatTypes, TypeParam> qs;
    FloatTypes const                obj{.id = 0, .precise = std::numbers::pi, .approx = std::numbers::e_v<float>};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_NEAR(selected.value().begin()->precise, std::numbers::pi, 1e-10);
    EXPECT_NEAR(selected.value().begin()->approx, std::numbers::e_v<float>, 1e-6);
}

TYPED_TEST(FloatTypesInsertUpdateTest, UpdateSingleFloatTypes) {
    QuerySet<FloatTypes, TypeParam> qs;
    FloatTypes const                obj{.id = 0, .precise = 1.0, .approx = 1.0F};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    FloatTypes const updated{
            .id = static_cast<int>(id), .precise = std::numbers::e, .approx = std::numbers::pi_v<float>
    };
    auto update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_NEAR(selected.value().begin()->precise, std::numbers::e, 1e-10);
    EXPECT_NEAR(selected.value().begin()->approx, std::numbers::pi_v<float>, 1e-6);
}

// ===== BOOLEAN AND STRING TYPES TESTS =====

template <typename ConnType> class MixedTypesInsertUpdateTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        const auto& conn_str = storm::test::get_connection_string<ConnType>();
        auto        result   = QuerySet<MixedTypes, ConnType>::set_default_connection(conn_str);
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<MixedTypes, ConnType>::get_default_connection();

        ASSERT_TRUE(
                storm::test::ensure_table<ConnType>(
                        conn,
                        "CREATE TABLE MixedTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, active INTEGER, name TEXT)"
                )
                        .has_value()
        );

        storm::test::begin_test_txn<ConnType>(conn, {"MixedTypes"});
    }
    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<MixedTypes, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<MixedTypes, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<MixedTypes, ConnType>::clear_default_connection();
    }
};

TYPED_TEST_SUITE(MixedTypesInsertUpdateTest, DatabaseTypes);

TYPED_TEST(MixedTypesInsertUpdateTest, InsertBooleanTrue) {
    QuerySet<MixedTypes, TypeParam> qs;
    MixedTypes const                obj{.id = 0, .active = true, .name = "active_user"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_TRUE(selected.value().begin()->active);
    EXPECT_EQ(selected.value().begin()->name, "active_user");
}

TYPED_TEST(MixedTypesInsertUpdateTest, InsertBooleanFalse) {
    QuerySet<MixedTypes, TypeParam> qs;
    MixedTypes const                obj{.id = 0, .active = false, .name = "inactive_user"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->active);
    EXPECT_EQ(selected.value().begin()->name, "inactive_user");
}

TYPED_TEST(MixedTypesInsertUpdateTest, UpdateBooleanAndString) {
    QuerySet<MixedTypes, TypeParam> qs;
    MixedTypes const                obj{.id = 0, .active = false, .name = "old_name"};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    MixedTypes const updated{.id = static_cast<int>(id), .active = true, .name = "new_name"};
    auto             update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_TRUE(selected.value().begin()->active);
    EXPECT_EQ(selected.value().begin()->name, "new_name");
}

TYPED_TEST(MixedTypesInsertUpdateTest, InsertBatchMixedTypes) {
    QuerySet<MixedTypes, TypeParam> qs;
    std::vector<MixedTypes>         batch = {{1, true, "user1"}, {2, false, "user2"}, {3, true, "user3"}};

    auto result = qs.insert(batch);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
    auto it = selected.value().begin();
    EXPECT_TRUE(it->active);
    ++it;
    EXPECT_FALSE(it->active);
    ++it;
    EXPECT_EQ(it->name, "user3");
}

// ===== OPTIONAL TYPES TESTS =====

template <typename ConnType> class OptTypesInsertUpdateTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        const auto& conn_str = storm::test::get_connection_string<ConnType>();
        auto        result   = QuerySet<OptTypes, ConnType>::set_default_connection(conn_str);
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<OptTypes, ConnType>::get_default_connection();

        ASSERT_TRUE(
                storm::test::ensure_table<ConnType>(
                        conn,
                        "CREATE TABLE OptTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, maybe_num INTEGER, name TEXT)"
                )
                        .has_value()
        );

        storm::test::begin_test_txn<ConnType>(conn, {"OptTypes"});
    }
    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<OptTypes, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<OptTypes, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<OptTypes, ConnType>::clear_default_connection();
    }
};

TYPED_TEST_SUITE(OptTypesInsertUpdateTest, DatabaseTypes);

TYPED_TEST(OptTypesInsertUpdateTest, InsertWithValues) {
    QuerySet<OptTypes, TypeParam> qs;
    OptTypes const                obj{.id = 0, .maybe_num = std::optional<int>(42), .name = "with_value"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(selected.value().begin()->maybe_num.has_value());
    EXPECT_EQ(selected.value().begin()->maybe_num.value(), 42);
    EXPECT_EQ(selected.value().begin()->name, "with_value");
}

TYPED_TEST(OptTypesInsertUpdateTest, InsertWithNull) {
    QuerySet<OptTypes, TypeParam> qs;
    OptTypes const                obj{.id = 0, .maybe_num = std::nullopt, .name = "null_value"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->maybe_num.has_value());
    EXPECT_EQ(selected.value().begin()->name, "null_value");
}

TYPED_TEST(OptTypesInsertUpdateTest, UpdateFromValueToNull) {
    QuerySet<OptTypes, TypeParam> qs;
    OptTypes const                obj{.id = 0, .maybe_num = std::optional<int>(100), .name = "original"};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    // Update to NULL
    OptTypes const updated{.id = static_cast<int>(id), .maybe_num = std::nullopt, .name = "updated_to_null"};
    auto           update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->maybe_num.has_value());
    EXPECT_EQ(selected.value().begin()->name, "updated_to_null");
}

TYPED_TEST(OptTypesInsertUpdateTest, UpdateFromNullToValue) {
    QuerySet<OptTypes, TypeParam> qs;
    OptTypes const                obj{.id = 0, .maybe_num = std::nullopt, .name = "null_start"};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    // Update from NULL to value
    OptTypes const updated{
            .id = static_cast<int>(id), .maybe_num = std::optional<int>(999), .name = "updated_to_value"
    };
    auto update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(selected.value().begin()->maybe_num.has_value());
    EXPECT_EQ(selected.value().begin()->maybe_num.value(), 999);
    EXPECT_EQ(selected.value().begin()->name, "updated_to_value");
}

TYPED_TEST(OptTypesInsertUpdateTest, InsertBatchMixedNulls) {
    QuerySet<OptTypes, TypeParam> qs;
    std::vector<OptTypes>         batch =
            {{1, std::optional<int>(1), "has_value"},
             {2, std::nullopt, "is_null"},
             {3, std::optional<int>(3), "has_value2"},
             {4, std::nullopt, "is_null2"}};

    auto result = qs.insert(batch);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 4);

    // Verify mixed NULL/value pattern
    auto it = selected.value().begin();
    EXPECT_TRUE(it->maybe_num.has_value());
    EXPECT_EQ(it->maybe_num.value(), 1);
    ++it;
    EXPECT_FALSE(it->maybe_num.has_value());
    ++it;
    EXPECT_TRUE(it->maybe_num.has_value());
    EXPECT_EQ(it->maybe_num.value(), 3);
    ++it;
    EXPECT_FALSE(it->maybe_num.has_value());
}

// ===== BLOB TYPES TESTS =====

template <typename ConnType> class DataTypesInsertUpdateTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        const auto& conn_str = storm::test::get_connection_string<ConnType>();
        auto        result   = QuerySet<DataTypes, ConnType>::set_default_connection(conn_str);
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<DataTypes, ConnType>::get_default_connection();

        ASSERT_TRUE(
                storm::test::ensure_table<ConnType>(
                        conn,
                        "CREATE TABLE DataTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, binary_data BLOB, label TEXT)"
                )
                        .has_value()
        );

        storm::test::begin_test_txn<ConnType>(conn, {"DataTypes"});
    }
    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<DataTypes, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<DataTypes, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<DataTypes, ConnType>::clear_default_connection();
    }
};

TYPED_TEST_SUITE(DataTypesInsertUpdateTest, DatabaseTypes);

TYPED_TEST(DataTypesInsertUpdateTest, InsertSmallBlob) {
    QuerySet<DataTypes, TypeParam> qs;
    DataTypes const                obj{.id = 0, .binary_data = {0xDE, 0xAD, 0xBE, 0xEF}, .label = "test_blob"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->binary_data, (std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));
    EXPECT_EQ(selected.value().begin()->label, "test_blob");
}

TYPED_TEST(DataTypesInsertUpdateTest, InsertLargeBlob) {
    QuerySet<DataTypes, TypeParam> qs;

    std::vector<uint8_t> large_data(1024);
    for (size_t i = 0; i < large_data.size(); ++i) {
        large_data[i] = static_cast<uint8_t>(i % 256);
    }

    DataTypes const obj{.id = 0, .binary_data = large_data, .label = "large"};
    auto            result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    auto it = selected.value().begin();
    ASSERT_EQ(it->binary_data.size(), 1024);

    for (size_t i = 0; i < 1024; ++i) {
        EXPECT_EQ(it->binary_data[i], static_cast<uint8_t>(i % 256));
    }
}

TYPED_TEST(DataTypesInsertUpdateTest, InsertEmptyBlob) {
    QuerySet<DataTypes, TypeParam> qs;
    DataTypes const                obj{.id = 0, .binary_data = {}, .label = "empty"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_TRUE(selected.value().begin()->binary_data.empty());
}

TYPED_TEST(DataTypesInsertUpdateTest, UpdateBlob) {
    QuerySet<DataTypes, TypeParam> qs;
    DataTypes const                obj{.id = 0, .binary_data = {0x01, 0x02}, .label = "original"};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    // Update with different blob
    DataTypes const updated{.id = static_cast<int>(id), .binary_data = {0xFF, 0xEE, 0xDD}, .label = "updated"};
    auto            update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->binary_data, (std::vector<uint8_t>{0xFF, 0xEE, 0xDD}));
    EXPECT_EQ(selected.value().begin()->label, "updated");
}

TYPED_TEST(DataTypesInsertUpdateTest, InsertBatchBlobs) {
    QuerySet<DataTypes, TypeParam> qs;
    std::vector<DataTypes> batch = {{1, {0x01}, "blob1"}, {2, {0x02, 0x03}, "blob2"}, {3, {0x04, 0x05, 0x06}, "blob3"}};

    auto result = qs.insert(batch);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
    auto it = selected.value().begin();
    EXPECT_EQ(it->binary_data.size(), 1);
    ++it;
    EXPECT_EQ(it->binary_data.size(), 2);
    ++it;
    EXPECT_EQ(it->binary_data.size(), 3);
}

// ===== EXTREME VALUE TESTS =====

TYPED_TEST(IntTypesInsertUpdateTest, ExtremeIntegerValues) {
    QuerySet<IntTypes, TypeParam> qs;

    // Min values
    IntTypes const min_obj{.id = 0, .big_num = -9223372036854775807LL - 1, .small_num = -32768};
    auto           result = qs.insert(min_obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    auto it = selected.value().begin();
    EXPECT_EQ(it->big_num, -9223372036854775807LL - 1);
    EXPECT_EQ(it->small_num, -32768);
}

TYPED_TEST(FloatTypesInsertUpdateTest, SpecialFloatValues) {
    QuerySet<FloatTypes, TypeParam> qs;

    // Zero, negative, very small
    FloatTypes const obj{.id = 0, .precise = 0.0, .approx = -0.0F};
    auto             result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_DOUBLE_EQ(selected.value().begin()->precise, 0.0);
}

// ===== INSERT OPTIONS TESTS =====

template <typename ConnType> class InsertOptionsTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        const auto& conn_str = storm::test::get_connection_string<ConnType>();
        auto        result   = QuerySet<IntTypes, ConnType>::set_default_connection(conn_str);
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<IntTypes, ConnType>::get_default_connection();

        ASSERT_TRUE(
                storm::test::ensure_table<ConnType>(
                        conn,
                        "CREATE TABLE IntTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, big_num INTEGER, "
                        "small_num INTEGER)"
                )
                        .has_value()
        );

        storm::test::begin_test_txn<ConnType>(conn, {"IntTypes"});
    }
    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<IntTypes, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<IntTypes, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<IntTypes, ConnType>::clear_default_connection();
    }
};

TYPED_TEST_SUITE(InsertOptionsTest, DatabaseTypes);

TYPED_TEST(InsertOptionsTest, BatchInsertReturnsVoid) {
    QuerySet<IntTypes, TypeParam> qs;
    std::vector<IntTypes>         batch = {{0, 100LL, 10}, {0, 200LL, 20}, {0, 300LL, 30}};

    // Batch insert returns void (no IDs - SQLite's last_insert_rowid is unreliable for batch)
    auto result = qs.insert(batch);
    ASSERT_TRUE(result.has_value());

    // Verify data was inserted by selecting
    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
}

TYPED_TEST(InsertOptionsTest, InsertWithCustomBatchSize) {
    QuerySet<IntTypes, TypeParam> qs;
    std::vector<IntTypes>         batch;

    // Create 100 objects
    batch.reserve(100);
    for (int i = 0; i < 100; ++i) {
        batch.emplace_back(0, static_cast<int64_t>(i) * 10, static_cast<short>(i));
    }

    // Use batch_size of 10
    auto result = qs.insert(batch, {{.batch_size = 10}});
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 100);
}

TYPED_TEST(InsertOptionsTest, InsertBatchSizeCappedToMax) {
    QuerySet<IntTypes, TypeParam> qs;
    std::vector<IntTypes>         batch = {{0, 100LL, 10}, {0, 200LL, 20}};

    // IntTypes has 2 non-PK fields, so max = 999/2 = 499
    // Request batch_size of 1000, should be capped to 499
    auto result = qs.insert(batch, {{.batch_size = 1000}});
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 2);
}

TYPED_TEST(InsertOptionsTest, SingleInsertReturnsId) {
    QuerySet<IntTypes, TypeParam> qs;
    IntTypes const                obj{.id = 0, .big_num = 999LL, .small_num = 99};

    // Single insert still returns the auto-generated ID
    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1);

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 1);
    EXPECT_EQ(selected.value().begin()->big_num, 999LL);
}

TYPED_TEST(InsertOptionsTest, LargeBatchWithCustomChunkSize) {
    QuerySet<IntTypes, TypeParam> qs;
    std::vector<IntTypes>         batch;

    // Create 1000 objects
    batch.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        batch.emplace_back(0, static_cast<int64_t>(i), static_cast<short>(i % 100));
    }

    // Use small batch_size to force multiple chunks
    auto result = qs.insert(batch, {{.batch_size = 50}});
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 1000);
}

TYPED_TEST(InsertOptionsTest, OptionsWithOnlyBatchSize) {
    QuerySet<IntTypes, TypeParam> qs;
    std::vector<IntTypes>         batch = {{0, 100LL, 10}, {0, 200LL, 20}};

    // Only specify batch_size
    auto result = qs.insert(batch, {{.batch_size = 1}});
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 2);
}

// ===== UNSIGNED INTEGER TYPES TESTS =====

// Unsigned integer types model
struct UnsignedTypes {
    [[= storm::meta::FieldAttr::primary]] int id{};
    unsigned int                              u_int{};
    unsigned short                            u_short{};
    unsigned long                             u_long{};
};

template <typename ConnType> class UnsignedTypesInsertUpdateTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        const auto& conn_str = storm::test::get_connection_string<ConnType>();
        auto        result   = QuerySet<UnsignedTypes, ConnType>::set_default_connection(conn_str);
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<UnsignedTypes, ConnType>::get_default_connection();

        ASSERT_TRUE(
                storm::test::ensure_table<ConnType>(
                        conn,
                        "CREATE TABLE UnsignedTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                        "u_int INTEGER, u_short INTEGER, u_long INTEGER)"
                )
                        .has_value()
        );

        storm::test::begin_test_txn<ConnType>(conn, {"UnsignedTypes"});
    }
    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<UnsignedTypes, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<UnsignedTypes, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<UnsignedTypes, ConnType>::clear_default_connection();
    }
};

TYPED_TEST_SUITE(UnsignedTypesInsertUpdateTest, DatabaseTypes);

TYPED_TEST(UnsignedTypesInsertUpdateTest, InsertUnsignedValues) {
    QuerySet<UnsignedTypes, TypeParam> qs;
    UnsignedTypes const                obj{.id = 0, .u_int = 4294967295U, .u_short = 65535, .u_long = 1000000UL};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    ASSERT_EQ(selected.value().size(), 1);
    EXPECT_EQ(selected.value().begin()->u_int, 4294967295U);
    EXPECT_EQ(selected.value().begin()->u_short, 65535);
    EXPECT_EQ(selected.value().begin()->u_long, 1000000UL);
}

TYPED_TEST(UnsignedTypesInsertUpdateTest, InsertBatchUnsigned) {
    QuerySet<UnsignedTypes, TypeParam> qs;
    std::vector<UnsignedTypes>         batch = {{0, 100U, 10, 1000UL}, {0, 200U, 20, 2000UL}, {0, 300U, 30, 3000UL}};

    auto result = qs.insert(batch);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
}

TYPED_TEST(UnsignedTypesInsertUpdateTest, UpdateUnsignedValues) {
    QuerySet<UnsignedTypes, TypeParam> qs;
    UnsignedTypes const                obj{.id = 0, .u_int = 100U, .u_short = 10, .u_long = 1000UL};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    UnsignedTypes const updated{.id = static_cast<int>(id), .u_int = 999U, .u_short = 99, .u_long = 9999UL};
    auto                update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->u_int, 999U);
    EXPECT_EQ(selected.value().begin()->u_short, 99);
    EXPECT_EQ(selected.value().begin()->u_long, 9999UL);
}

// ===== LONG LONG TYPES TESTS =====

// Long long types model (for 64-bit signed/unsigned coverage)
struct LongLongTypes {
    [[= storm::meta::FieldAttr::primary]] int id{};
    long long                                 ll_signed{};
    unsigned long long                        ll_unsigned{};
};

template <typename ConnType> class LongLongTypesInsertUpdateTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        if (!storm::test::backend_available<ConnType>()) {
            GTEST_SKIP() << "PostgreSQL unavailable";
        }

        const auto& conn_str = storm::test::get_connection_string<ConnType>();
        auto        result   = QuerySet<LongLongTypes, ConnType>::set_default_connection(conn_str);
        ASSERT_TRUE(result.has_value()) << "Failed to open database: " << result.error().message();

        const auto& conn = QuerySet<LongLongTypes, ConnType>::get_default_connection();

        ASSERT_TRUE(
                storm::test::ensure_table<ConnType>(
                        conn,
                        "CREATE TABLE LongLongTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                        "ll_signed INTEGER, ll_unsigned INTEGER)"
                )
                        .has_value()
        );

        storm::test::begin_test_txn<ConnType>(conn, {"LongLongTypes"});
    }
    auto TearDown() -> void override {
        if constexpr (storm::test::is_postgresql<ConnType>()) {
            if (QuerySet<LongLongTypes, ConnType>::has_default_connection()) {
                const auto& conn = QuerySet<LongLongTypes, ConnType>::get_default_connection();
                storm::test::rollback_test_txn<ConnType>(conn);
            }
        }
        QuerySet<LongLongTypes, ConnType>::clear_default_connection();
    }
};

TYPED_TEST_SUITE(LongLongTypesInsertUpdateTest, DatabaseTypes);

TYPED_TEST(LongLongTypesInsertUpdateTest, InsertLongLongValues) {
    QuerySet<LongLongTypes, TypeParam> qs;
    LongLongTypes const obj{.id = 0, .ll_signed = 9223372036854775807LL, .ll_unsigned = 9223372036854775807ULL};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    ASSERT_EQ(selected.value().size(), 1);
    EXPECT_EQ(selected.value().begin()->ll_signed, 9223372036854775807LL);
    // Note: SQLite stores as int64_t, so very large unsigned values wrap around
    EXPECT_EQ(selected.value().begin()->ll_unsigned, 9223372036854775807ULL);
}

TYPED_TEST(LongLongTypesInsertUpdateTest, InsertNegativeLongLong) {
    QuerySet<LongLongTypes, TypeParam> qs;
    LongLongTypes const                obj{.id = 0, .ll_signed = -9223372036854775807LL, .ll_unsigned = 0ULL};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->ll_signed, -9223372036854775807LL);
    EXPECT_EQ(selected.value().begin()->ll_unsigned, 0ULL);
}

TYPED_TEST(LongLongTypesInsertUpdateTest, UpdateLongLongValues) {
    QuerySet<LongLongTypes, TypeParam> qs;
    LongLongTypes const                obj{.id = 0, .ll_signed = 100LL, .ll_unsigned = 200ULL};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    LongLongTypes const updated{.id = static_cast<int>(id), .ll_signed = -999LL, .ll_unsigned = 999ULL};
    auto                update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->ll_signed, -999LL);
    EXPECT_EQ(selected.value().begin()->ll_unsigned, 999ULL);
}

// Note: WHERE clause tests with unsigned types are not included because the
// ExpressionVariant doesn't support unsigned types directly. INSERT/UPDATE work
// with unsigned types through bind_parameter_value which casts them appropriately.

// ===== ERROR HANDLING TESTS =====

// Test Error struct accessors for code coverage
TEST(ErrorHandlingTest, ErrorAccessors) {
    // Try to open a database in a non-existent directory (should fail)
    auto result = QuerySet<IntTypes>::set_default_connection("/nonexistent/path/database.db");

    // Verify operation failed
    ASSERT_FALSE(result.has_value()) << "Should fail to open database in non-existent path";

    // Test Error accessors (coverage for Error::code() and Error::message())
    const auto& error = result.error();
    EXPECT_NE(error.code(), 0) << "Error code should be non-zero for failed operation";
    EXPECT_FALSE(error.message().empty()) << "Error message should not be empty";

    // Verify message is meaningful
    std::string_view msg = error.message();
    EXPECT_GT(msg.size(), 5) << "Error message should be descriptive";
}

// Test select on non-existent table
TEST(ErrorHandlingTest, SelectFromNonExistentTable) {
    // Set up in-memory database (no table created)
    auto conn_result = QuerySet<IntTypes>::set_default_connection(storm::test::get_connection_string());
    ASSERT_TRUE(conn_result.has_value());

    // Try to select from non-existent table
    QuerySet<IntTypes> qs;
    auto               select_result = qs.select();

    // Should fail because table doesn't exist
    ASSERT_FALSE(select_result.has_value()) << "Should fail to select from non-existent table";

    // Test error accessors
    const auto& error = select_result.error();
    EXPECT_NE(error.code(), 0);
    EXPECT_FALSE(error.message().empty());

    QuerySet<IntTypes>::clear_default_connection();
}

// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
