#include <gtest/gtest.h>

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
    [[= storm::meta::FieldAttr::primary]] int id;
    bool                                      active;
    std::string                               name;
};

// Optional types model (simplified to avoid compiler constexpr bug)
struct OptTypes {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::optional<int>                        maybe_num;
    std::string                               name; // Use regular string instead of optional to reduce complexity
};

// BLOB types model
struct DataTypes {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::vector<uint8_t>                      binary;
    std::string                               label;
};

// ===== INTEGER TYPES TESTS =====

class IntTypesInsertUpdateTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto result = QuerySet<IntTypes>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());
        auto& conn = QuerySet<IntTypes>::get_default_connection();
        ASSERT_TRUE(conn.execute(
                                "CREATE TABLE IntTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, big_num INTEGER, "
                                "small_num INTEGER)"
        )
                            .has_value());
    }
    void TearDown() override {
        QuerySet<IntTypes>::clear_default_connection();
    }
};

TEST_F(IntTypesInsertUpdateTest, InsertSingleIntTypes) {
    QuerySet<IntTypes> qs;
    IntTypes           obj{0, 9223372036854775807LL, 32767};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    ASSERT_EQ(selected.value().size(), 1);
    EXPECT_EQ(selected.value()[0].big_num, 9223372036854775807LL);
    EXPECT_EQ(selected.value()[0].small_num, 32767);
}

TEST_F(IntTypesInsertUpdateTest, InsertBatchIntTypes) {
    QuerySet<IntTypes>    qs;
    std::vector<IntTypes> batch = {{1, 100LL, 10}, {2, 200LL, 20}, {3, 300LL, 30}};

    auto result = qs.insert(std::span<const IntTypes>(batch));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), 3);

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
    EXPECT_EQ(selected.value()[1].big_num, 200LL);
}

TEST_F(IntTypesInsertUpdateTest, UpdateSingleIntTypes) {
    QuerySet<IntTypes> qs;
    IntTypes           obj{0, 100LL, 10};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t id = insert_result.value();

    // Verify INSERT worked
    auto check1 = qs.select();
    ASSERT_TRUE(check1.has_value());
    EXPECT_EQ(check1.value()[0].big_num, 100LL);
    EXPECT_EQ(check1.value()[0].small_num, 10);

    // Update with the returned ID
    IntTypes updated{static_cast<int>(id), 999LL, 99};
    auto     update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value()[0].big_num, 999LL);
    EXPECT_EQ(selected.value()[0].small_num, 99);
}

TEST_F(IntTypesInsertUpdateTest, UpdateBatchIntTypes) {
    QuerySet<IntTypes>    qs;
    std::vector<IntTypes> batch = {{1, 100LL, 10}, {2, 200LL, 20}, {3, 300LL, 30}};

    auto insert_result = qs.insert(std::span<const IntTypes>(batch));
    ASSERT_TRUE(insert_result.has_value());

    // Update all
    std::vector<IntTypes> updates       = {{1, 111LL, 11}, {2, 222LL, 22}, {3, 333LL, 33}};
    auto                  update_result = qs.update(std::span<const IntTypes>(updates));
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value()[0].big_num, 111LL);
    EXPECT_EQ(selected.value()[1].big_num, 222LL);
    EXPECT_EQ(selected.value()[2].big_num, 333LL);
}

// ===== FLOATING POINT TYPES TESTS =====

class FloatTypesInsertUpdateTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto result = QuerySet<FloatTypes>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());
        auto& conn = QuerySet<FloatTypes>::get_default_connection();
        ASSERT_TRUE(
                conn.execute(
                            "CREATE TABLE FloatTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, precise REAL, approx REAL)"
                )
                        .has_value()
        );
    }
    void TearDown() override {
        QuerySet<FloatTypes>::clear_default_connection();
    }
};

TEST_F(FloatTypesInsertUpdateTest, InsertSingleFloatTypes) {
    QuerySet<FloatTypes> qs;
    FloatTypes           obj{0, 3.141592653589793, 2.718f};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_NEAR(selected.value()[0].precise, 3.141592653589793, 1e-10);
    EXPECT_NEAR(selected.value()[0].approx, 2.718f, 1e-4);
}

TEST_F(FloatTypesInsertUpdateTest, UpdateSingleFloatTypes) {
    QuerySet<FloatTypes> qs;
    FloatTypes           obj{0, 1.0, 1.0f};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t id = insert_result.value();

    FloatTypes updated{static_cast<int>(id), 2.71828, 3.14159f};
    auto       update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_NEAR(selected.value()[0].precise, 2.71828, 1e-5);
    EXPECT_NEAR(selected.value()[0].approx, 3.14159f, 1e-4);
}

// ===== BOOLEAN AND STRING TYPES TESTS =====

class MixedTypesInsertUpdateTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto result = QuerySet<MixedTypes>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());
        auto& conn = QuerySet<MixedTypes>::get_default_connection();
        ASSERT_TRUE(
                conn.execute(
                            "CREATE TABLE MixedTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, active INTEGER, name TEXT)"
                )
                        .has_value()
        );
    }
    void TearDown() override {
        QuerySet<MixedTypes>::clear_default_connection();
    }
};

TEST_F(MixedTypesInsertUpdateTest, InsertBooleanTrue) {
    QuerySet<MixedTypes> qs;
    MixedTypes           obj{0, true, "active_user"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_TRUE(selected.value()[0].active);
    EXPECT_EQ(selected.value()[0].name, "active_user");
}

TEST_F(MixedTypesInsertUpdateTest, InsertBooleanFalse) {
    QuerySet<MixedTypes> qs;
    MixedTypes           obj{0, false, "inactive_user"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value()[0].active);
    EXPECT_EQ(selected.value()[0].name, "inactive_user");
}

TEST_F(MixedTypesInsertUpdateTest, UpdateBooleanAndString) {
    QuerySet<MixedTypes> qs;
    MixedTypes           obj{0, false, "old_name"};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t id = insert_result.value();

    MixedTypes updated{static_cast<int>(id), true, "new_name"};
    auto       update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_TRUE(selected.value()[0].active);
    EXPECT_EQ(selected.value()[0].name, "new_name");
}

TEST_F(MixedTypesInsertUpdateTest, InsertBatchMixedTypes) {
    QuerySet<MixedTypes>    qs;
    std::vector<MixedTypes> batch = {{1, true, "user1"}, {2, false, "user2"}, {3, true, "user3"}};

    auto result = qs.insert(std::span<const MixedTypes>(batch));
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
    EXPECT_TRUE(selected.value()[0].active);
    EXPECT_FALSE(selected.value()[1].active);
    EXPECT_EQ(selected.value()[2].name, "user3");
}

// ===== OPTIONAL TYPES TESTS =====

class OptTypesInsertUpdateTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto result = QuerySet<OptTypes>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());
        auto& conn = QuerySet<OptTypes>::get_default_connection();
        ASSERT_TRUE(
                conn.execute(
                            "CREATE TABLE OptTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, maybe_num INTEGER, name TEXT)"
                )
                        .has_value()
        );
    }
    void TearDown() override {
        QuerySet<OptTypes>::clear_default_connection();
    }
};

TEST_F(OptTypesInsertUpdateTest, InsertWithValues) {
    QuerySet<OptTypes> qs;
    OptTypes           obj{0, std::optional<int>(42), "with_value"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(selected.value()[0].maybe_num.has_value());
    EXPECT_EQ(selected.value()[0].maybe_num.value(), 42);
    EXPECT_EQ(selected.value()[0].name, "with_value");
}

TEST_F(OptTypesInsertUpdateTest, InsertWithNull) {
    QuerySet<OptTypes> qs;
    OptTypes           obj{0, std::nullopt, "null_value"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value()[0].maybe_num.has_value());
    EXPECT_EQ(selected.value()[0].name, "null_value");
}

TEST_F(OptTypesInsertUpdateTest, UpdateFromValueToNull) {
    QuerySet<OptTypes> qs;
    OptTypes           obj{0, std::optional<int>(100), "original"};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t id = insert_result.value();

    // Update to NULL
    OptTypes updated{static_cast<int>(id), std::nullopt, "updated_to_null"};
    auto     update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value()[0].maybe_num.has_value());
    EXPECT_EQ(selected.value()[0].name, "updated_to_null");
}

TEST_F(OptTypesInsertUpdateTest, UpdateFromNullToValue) {
    QuerySet<OptTypes> qs;
    OptTypes           obj{0, std::nullopt, "null_start"};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t id = insert_result.value();

    // Update from NULL to value
    OptTypes updated{static_cast<int>(id), std::optional<int>(999), "updated_to_value"};
    auto     update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(selected.value()[0].maybe_num.has_value());
    EXPECT_EQ(selected.value()[0].maybe_num.value(), 999);
    EXPECT_EQ(selected.value()[0].name, "updated_to_value");
}

TEST_F(OptTypesInsertUpdateTest, InsertBatchMixedNulls) {
    QuerySet<OptTypes>    qs;
    std::vector<OptTypes> batch =
            {{1, std::optional<int>(1), "has_value"},
             {2, std::nullopt, "is_null"},
             {3, std::optional<int>(3), "has_value2"},
             {4, std::nullopt, "is_null2"}};

    auto result = qs.insert(std::span<const OptTypes>(batch));
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 4);

    // Verify mixed NULL/value pattern
    EXPECT_TRUE(selected.value()[0].maybe_num.has_value());
    EXPECT_EQ(selected.value()[0].maybe_num.value(), 1);
    EXPECT_FALSE(selected.value()[1].maybe_num.has_value());
    EXPECT_TRUE(selected.value()[2].maybe_num.has_value());
    EXPECT_EQ(selected.value()[2].maybe_num.value(), 3);
    EXPECT_FALSE(selected.value()[3].maybe_num.has_value());
}

// ===== BLOB TYPES TESTS =====

class DataTypesInsertUpdateTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto result = QuerySet<DataTypes>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());
        auto& conn = QuerySet<DataTypes>::get_default_connection();
        ASSERT_TRUE(
                conn.execute("CREATE TABLE DataTypes (id INTEGER PRIMARY KEY AUTOINCREMENT, binary BLOB, label TEXT)")
                        .has_value()
        );
    }
    void TearDown() override {
        QuerySet<DataTypes>::clear_default_connection();
    }
};

TEST_F(DataTypesInsertUpdateTest, InsertSmallBlob) {
    QuerySet<DataTypes> qs;
    DataTypes           obj{0, {0xDE, 0xAD, 0xBE, 0xEF}, "test_blob"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value()[0].binary, (std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));
    EXPECT_EQ(selected.value()[0].label, "test_blob");
}

TEST_F(DataTypesInsertUpdateTest, InsertLargeBlob) {
    QuerySet<DataTypes> qs;

    std::vector<uint8_t> large_data(1024);
    for (size_t i = 0; i < large_data.size(); ++i) {
        large_data[i] = static_cast<uint8_t>(i % 256);
    }

    DataTypes obj{0, large_data, "large"};
    auto      result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    ASSERT_EQ(selected.value()[0].binary.size(), 1024);

    for (size_t i = 0; i < 1024; ++i) {
        EXPECT_EQ(selected.value()[0].binary[i], static_cast<uint8_t>(i % 256));
    }
}

TEST_F(DataTypesInsertUpdateTest, InsertEmptyBlob) {
    QuerySet<DataTypes> qs;
    DataTypes           obj{0, {}, "empty"};

    auto result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_TRUE(selected.value()[0].binary.empty());
}

TEST_F(DataTypesInsertUpdateTest, UpdateBlob) {
    QuerySet<DataTypes> qs;
    DataTypes           obj{0, {0x01, 0x02}, "original"};

    auto insert_result = qs.insert(obj);
    ASSERT_TRUE(insert_result.has_value());
    int64_t id = insert_result.value();

    // Update with different blob
    DataTypes updated{static_cast<int>(id), {0xFF, 0xEE, 0xDD}, "updated"};
    auto      update_result = qs.update(updated);
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value()[0].binary, (std::vector<uint8_t>{0xFF, 0xEE, 0xDD}));
    EXPECT_EQ(selected.value()[0].label, "updated");
}

TEST_F(DataTypesInsertUpdateTest, InsertBatchBlobs) {
    QuerySet<DataTypes>    qs;
    std::vector<DataTypes> batch = {{1, {0x01}, "blob1"}, {2, {0x02, 0x03}, "blob2"}, {3, {0x04, 0x05, 0x06}, "blob3"}};

    auto result = qs.insert(std::span<const DataTypes>(batch));
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
    EXPECT_EQ(selected.value()[0].binary.size(), 1);
    EXPECT_EQ(selected.value()[1].binary.size(), 2);
    EXPECT_EQ(selected.value()[2].binary.size(), 3);
}

// ===== EXTREME VALUE TESTS =====

TEST_F(IntTypesInsertUpdateTest, ExtremeIntegerValues) {
    QuerySet<IntTypes> qs;

    // Min values
    IntTypes min_obj{0, -9223372036854775807LL - 1, -32768};
    auto     result = qs.insert(min_obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value()[0].big_num, -9223372036854775807LL - 1);
    EXPECT_EQ(selected.value()[0].small_num, -32768);
}

TEST_F(FloatTypesInsertUpdateTest, SpecialFloatValues) {
    QuerySet<FloatTypes> qs;

    // Zero, negative, very small
    FloatTypes obj{0, 0.0, -0.0f};
    auto       result = qs.insert(obj);
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select();
    ASSERT_TRUE(selected.has_value());
    EXPECT_DOUBLE_EQ(selected.value()[0].precise, 0.0);
}
