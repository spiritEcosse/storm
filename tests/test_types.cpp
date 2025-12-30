#include <gtest/gtest.h>

#include <numbers>

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
    std::vector<uint8_t>                      binary;
    std::string                               label;
};

// ===== INTEGER TYPES TESTS =====

class IntTypesInsertUpdateTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        auto result = QuerySet<IntTypes>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());
        const auto& conn = QuerySet<IntTypes>::get_default_connection();
        ASSERT_TRUE(conn->execute(
                                "CREATE TABLE IntTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, big_num INTEGER, "
                                "small_num INTEGER)"
        )
                            .has_value());
    }
    auto TearDown() -> void override {
        QuerySet<IntTypes>::clear_default_connection();
    }
};

TEST_F(IntTypesInsertUpdateTest, InsertSingleIntTypes) {
    QuerySet<IntTypes> qs;
    IntTypes const     obj{.id = 0, .big_num = 9223372036854775807LL, .small_num = 32767};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    ASSERT_EQ(selected.value().size(), 1);
    EXPECT_EQ(selected.value().begin()->big_num, 9223372036854775807LL);
    EXPECT_EQ(selected.value().begin()->small_num, 32767);
}

TEST_F(IntTypesInsertUpdateTest, InsertBatchIntTypes) {
    QuerySet<IntTypes>    qs;
    std::vector<IntTypes> batch = {{0, 100LL, 10}, {0, 200LL, 20}, {0, 300LL, 30}};

    auto result = qs.insert(batch);
    ASSERT_TRUE(result.has_value());

    // Batch insert returns void, verify via SELECT
    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
    EXPECT_EQ(selected.value().begin()->big_num, 100LL);
}

TEST_F(IntTypesInsertUpdateTest, UpdateSingleIntTypes) {
    QuerySet<IntTypes> qs;
    IntTypes const     obj{.id = 0, .big_num = 100LL, .small_num = 10};

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

TEST_F(IntTypesInsertUpdateTest, UpdateBatchIntTypes) {
    QuerySet<IntTypes>    qs;
    std::vector<IntTypes> batch = {{1, 100LL, 10}, {2, 200LL, 20}, {3, 300LL, 30}};

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

class FloatTypesInsertUpdateTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        auto result = QuerySet<FloatTypes>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());
        const auto& conn = QuerySet<FloatTypes>::get_default_connection();
        ASSERT_TRUE(
                conn->execute(
                            "CREATE TABLE FloatTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, precise REAL, approx REAL)"
                )
                        .has_value()
        );
    }
    auto TearDown() -> void override {
        QuerySet<FloatTypes>::clear_default_connection();
    }
};

TEST_F(FloatTypesInsertUpdateTest, InsertSingleFloatTypes) {
    QuerySet<FloatTypes> qs;
    FloatTypes const     obj{.id = 0, .precise = std::numbers::pi, .approx = std::numbers::e_v<float>};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_NEAR(selected.value().begin()->precise, std::numbers::pi, 1e-10);
    EXPECT_NEAR(selected.value().begin()->approx, std::numbers::e_v<float>, 1e-6);
}

TEST_F(FloatTypesInsertUpdateTest, UpdateSingleFloatTypes) {
    QuerySet<FloatTypes> qs;
    FloatTypes const     obj{.id = 0, .precise = 1.0, .approx = 1.0F};

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

class MixedTypesInsertUpdateTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        auto result = QuerySet<MixedTypes>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());
        const auto& conn = QuerySet<MixedTypes>::get_default_connection();
        ASSERT_TRUE(
                conn->execute(
                            "CREATE TABLE MixedTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, active INTEGER, name TEXT)"
                )
                        .has_value()
        );
    }
    auto TearDown() -> void override {
        QuerySet<MixedTypes>::clear_default_connection();
    }
};

TEST_F(MixedTypesInsertUpdateTest, InsertBooleanTrue) {
    QuerySet<MixedTypes> qs;
    MixedTypes const     obj{.id = 0, .active = true, .name = "active_user"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_TRUE(selected.value().begin()->active);
    EXPECT_EQ(selected.value().begin()->name, "active_user");
}

TEST_F(MixedTypesInsertUpdateTest, InsertBooleanFalse) {
    QuerySet<MixedTypes> qs;
    MixedTypes const     obj{.id = 0, .active = false, .name = "inactive_user"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->active);
    EXPECT_EQ(selected.value().begin()->name, "inactive_user");
}

TEST_F(MixedTypesInsertUpdateTest, UpdateBooleanAndString) {
    QuerySet<MixedTypes> qs;
    MixedTypes const     obj{.id = 0, .active = false, .name = "old_name"};

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

TEST_F(MixedTypesInsertUpdateTest, InsertBatchMixedTypes) {
    QuerySet<MixedTypes>    qs;
    std::vector<MixedTypes> batch = {{1, true, "user1"}, {2, false, "user2"}, {3, true, "user3"}};

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

class OptTypesInsertUpdateTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        auto result = QuerySet<OptTypes>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());
        const auto& conn = QuerySet<OptTypes>::get_default_connection();
        ASSERT_TRUE(
                conn->execute(
                            "CREATE TABLE OptTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, maybe_num INTEGER, name TEXT)"
                )
                        .has_value()
        );
    }
    auto TearDown() -> void override {
        QuerySet<OptTypes>::clear_default_connection();
    }
};

TEST_F(OptTypesInsertUpdateTest, InsertWithValues) {
    QuerySet<OptTypes> qs;
    OptTypes const     obj{.id = 0, .maybe_num = std::optional<int>(42), .name = "with_value"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(selected.value().begin()->maybe_num.has_value());
    EXPECT_EQ(selected.value().begin()->maybe_num.value(), 42);
    EXPECT_EQ(selected.value().begin()->name, "with_value");
}

TEST_F(OptTypesInsertUpdateTest, InsertWithNull) {
    QuerySet<OptTypes> qs;
    OptTypes const     obj{.id = 0, .maybe_num = std::nullopt, .name = "null_value"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->maybe_num.has_value());
    EXPECT_EQ(selected.value().begin()->name, "null_value");
}

TEST_F(OptTypesInsertUpdateTest, UpdateFromValueToNull) {
    QuerySet<OptTypes> qs;
    OptTypes const     obj{.id = 0, .maybe_num = std::optional<int>(100), .name = "original"};

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

TEST_F(OptTypesInsertUpdateTest, UpdateFromNullToValue) {
    QuerySet<OptTypes> qs;
    OptTypes const     obj{.id = 0, .maybe_num = std::nullopt, .name = "null_start"};

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

TEST_F(OptTypesInsertUpdateTest, InsertBatchMixedNulls) {
    QuerySet<OptTypes>    qs;
    std::vector<OptTypes> batch =
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

class DataTypesInsertUpdateTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        auto result = QuerySet<DataTypes>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());
        const auto& conn = QuerySet<DataTypes>::get_default_connection();
        ASSERT_TRUE(
                conn->execute("CREATE TABLE DataTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, binary BLOB, label TEXT)")
                        .has_value()
        );
    }
    auto TearDown() -> void override {
        QuerySet<DataTypes>::clear_default_connection();
    }
};

TEST_F(DataTypesInsertUpdateTest, InsertSmallBlob) {
    QuerySet<DataTypes> qs;
    DataTypes const     obj{.id = 0, .binary = {0xDE, 0xAD, 0xBE, 0xEF}, .label = "test_blob"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->binary, (std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));
    EXPECT_EQ(selected.value().begin()->label, "test_blob");
}

TEST_F(DataTypesInsertUpdateTest, InsertLargeBlob) {
    QuerySet<DataTypes> qs;

    std::vector<uint8_t> large_data(1024);
    for (size_t i = 0; i < large_data.size(); ++i) {
        large_data[i] = static_cast<uint8_t>(i % 256);
    }

    DataTypes const obj{.id = 0, .binary = large_data, .label = "large"};
    auto            result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    auto it = selected.value().begin();
    ASSERT_EQ(it->binary.size(), 1024);

    for (size_t i = 0; i < 1024; ++i) {
        EXPECT_EQ(it->binary[i], static_cast<uint8_t>(i % 256));
    }
}

TEST_F(DataTypesInsertUpdateTest, InsertEmptyBlob) {
    QuerySet<DataTypes> qs;
    DataTypes const     obj{.id = 0, .binary = {}, .label = "empty"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_TRUE(selected.value().begin()->binary.empty());
}

TEST_F(DataTypesInsertUpdateTest, UpdateBlob) {
    QuerySet<DataTypes> qs;
    DataTypes const     obj{.id = 0, .binary = {0x01, 0x02}, .label = "original"};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    // Update with different blob
    DataTypes const updated{.id = static_cast<int>(id), .binary = {0xFF, 0xEE, 0xDD}, .label = "updated"};
    auto            update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->binary, (std::vector<uint8_t>{0xFF, 0xEE, 0xDD}));
    EXPECT_EQ(selected.value().begin()->label, "updated");
}

TEST_F(DataTypesInsertUpdateTest, InsertBatchBlobs) {
    QuerySet<DataTypes>    qs;
    std::vector<DataTypes> batch = {{1, {0x01}, "blob1"}, {2, {0x02, 0x03}, "blob2"}, {3, {0x04, 0x05, 0x06}, "blob3"}};

    auto result = qs.insert(batch);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
    auto it = selected.value().begin();
    EXPECT_EQ(it->binary.size(), 1);
    ++it;
    EXPECT_EQ(it->binary.size(), 2);
    ++it;
    EXPECT_EQ(it->binary.size(), 3);
}

// ===== EXTREME VALUE TESTS =====

TEST_F(IntTypesInsertUpdateTest, ExtremeIntegerValues) {
    QuerySet<IntTypes> qs;

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

TEST_F(FloatTypesInsertUpdateTest, SpecialFloatValues) {
    QuerySet<FloatTypes> qs;

    // Zero, negative, very small
    FloatTypes const obj{.id = 0, .precise = 0.0, .approx = -0.0F};
    auto             result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_DOUBLE_EQ(selected.value().begin()->precise, 0.0);
}

// ===== INSERT OPTIONS TESTS =====

class InsertOptionsTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override {
        auto result = QuerySet<IntTypes>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());
        const auto& conn = QuerySet<IntTypes>::get_default_connection();
        ASSERT_TRUE(conn->execute(
                                "CREATE TABLE IntTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, big_num INTEGER, "
                                "small_num INTEGER)"
        )
                            .has_value());
    }
    auto TearDown() -> void override {
        QuerySet<IntTypes>::clear_default_connection();
    }
};

TEST_F(InsertOptionsTest, BatchInsertReturnsVoid) {
    QuerySet<IntTypes>    qs;
    std::vector<IntTypes> batch = {{0, 100LL, 10}, {0, 200LL, 20}, {0, 300LL, 30}};

    // Batch insert returns void (no IDs - SQLite's last_insert_rowid is unreliable for batch)
    auto result = qs.insert(batch);
    ASSERT_TRUE(result.has_value());

    // Verify data was inserted by selecting
    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
}

TEST_F(InsertOptionsTest, InsertWithCustomBatchSize) {
    QuerySet<IntTypes>    qs;
    std::vector<IntTypes> batch;

    // Create 100 objects
    batch.reserve(100);
    for (int i = 0; i < 100; ++i) {
        batch.emplace_back(0, static_cast<int64_t>(i * 10), static_cast<short>(i));
    }

    // Use batch_size of 10
    auto result = qs.insert(batch, {{.batch_size = 10}});
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 100);
}

TEST_F(InsertOptionsTest, InsertBatchSizeCappedToMax) {
    QuerySet<IntTypes>    qs;
    std::vector<IntTypes> batch = {{0, 100LL, 10}, {0, 200LL, 20}};

    // IntTypes has 2 non-PK fields, so max = 999/2 = 499
    // Request batch_size of 1000, should be capped to 499
    auto result = qs.insert(batch, {{.batch_size = 1000}});
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 2);
}

TEST_F(InsertOptionsTest, SingleInsertReturnsId) {
    QuerySet<IntTypes> qs;
    IntTypes const     obj{.id = 0, .big_num = 999LL, .small_num = 99};

    // Single insert still returns the auto-generated ID
    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1);

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 1);
    EXPECT_EQ(selected.value().begin()->big_num, 999LL);
}

TEST_F(InsertOptionsTest, LargeBatchWithCustomChunkSize) {
    QuerySet<IntTypes>    qs;
    std::vector<IntTypes> batch;

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

TEST_F(InsertOptionsTest, OptionsWithOnlyBatchSize) {
    QuerySet<IntTypes>    qs;
    std::vector<IntTypes> batch = {{0, 100LL, 10}, {0, 200LL, 20}};

    // Only specify batch_size
    auto result = qs.insert(batch, {{.batch_size = 1}});
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 2);
}
