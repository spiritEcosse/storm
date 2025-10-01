#include <gtest/gtest.h>

import storm;
import <string>;
import <vector>;
import <expected>;
import <optional>;
import <cstdint>;

using namespace storm;

// Test models - using smaller structs to avoid compiler constexpr evaluation issues

struct IntegerTypes {
    [[= storm::meta::FieldAttr::primary]] int id;
    int64_t        int64_field;
    short          short_field;
    unsigned int   uint_field;
};

struct FloatingTypes {
    [[= storm::meta::FieldAttr::primary]] int id;
    double         double_field;
    float          float_field;
};

struct BooleanType {
    [[= storm::meta::FieldAttr::primary]] int id;
    bool           bool_field;
    std::string    name;
};

struct OptionalTypes {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::optional<int>         opt_int_field;
    std::optional<std::string> opt_string_field;
};

struct BlobType {
    [[= storm::meta::FieldAttr::primary]] int id;
    std::vector<uint8_t>       blob_field;
    std::string                name;
};

// Test fixture for integer types
class IntegerTypesTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto result = QuerySet<IntegerTypes>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());

        auto& conn = QuerySet<IntegerTypes>::get_default_connection();
        auto create_result = conn.execute(
                "CREATE TABLE IntegerTypes ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "int64_field INTEGER NOT NULL, "
                "short_field INTEGER NOT NULL, "
                "uint_field INTEGER NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_result.has_value());
    }

    void TearDown() override {
        QuerySet<IntegerTypes>::clear_default_connection();
    }
};

// Test fixture for floating types
class FloatingTypesTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto result = QuerySet<FloatingTypes>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());

        auto& conn = QuerySet<FloatingTypes>::get_default_connection();
        auto create_result = conn.execute(
                "CREATE TABLE FloatingTypes ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "double_field REAL NOT NULL, "
                "float_field REAL NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_result.has_value());
    }

    void TearDown() override {
        QuerySet<FloatingTypes>::clear_default_connection();
    }
};

// Test fixture for boolean type
class BooleanTypeTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto result = QuerySet<BooleanType>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());

        auto& conn = QuerySet<BooleanType>::get_default_connection();
        auto create_result = conn.execute(
                "CREATE TABLE BooleanType ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "bool_field INTEGER NOT NULL, "
                "name TEXT NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_result.has_value());
    }

    void TearDown() override {
        QuerySet<BooleanType>::clear_default_connection();
    }
};

// Test fixture for optional types
class OptionalTypesTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto result = QuerySet<OptionalTypes>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());

        auto& conn = QuerySet<OptionalTypes>::get_default_connection();
        auto create_result = conn.execute(
                "CREATE TABLE OptionalTypes ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "opt_int_field INTEGER, "
                "opt_string_field TEXT"
                ")"
        );
        ASSERT_TRUE(create_result.has_value());
    }

    void TearDown() override {
        QuerySet<OptionalTypes>::clear_default_connection();
    }
};

// Test fixture for BLOB type
class BlobTypeTest : public ::testing::Test {
  protected:
    void SetUp() override {
        auto result = QuerySet<BlobType>::set_default_connection(":memory:");
        ASSERT_TRUE(result.has_value());

        auto& conn = QuerySet<BlobType>::get_default_connection();
        auto create_result = conn.execute(
                "CREATE TABLE BlobType ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "blob_field BLOB NOT NULL, "
                "name TEXT NOT NULL"
                ")"
        );
        ASSERT_TRUE(create_result.has_value());
    }

    void TearDown() override {
        QuerySet<BlobType>::clear_default_connection();
    }
};

// ===== INTEGER TYPES TESTS =====

TEST_F(IntegerTypesTest, InsertAndSelectIntegerTypes) {
    QuerySet<IntegerTypes> queryset;

    IntegerTypes test_obj{
        0,                           // id
        9223372036854775807LL,       // int64_field (max int64)
        32767,                       // short_field (max short)
        4000000000U                  // uint_field
    };

    auto insert_result = queryset.insert(test_obj);
    ASSERT_TRUE(insert_result.has_value());

    auto select_result = queryset.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& results = select_result.value();
    ASSERT_EQ(results.size(), 1);

    const auto& obj = results[0];
    EXPECT_EQ(obj.int64_field, 9223372036854775807LL);
    EXPECT_EQ(obj.short_field, 32767);
    EXPECT_EQ(obj.uint_field, 4000000000U);
}

TEST_F(IntegerTypesTest, ExtremeIntegerValues) {
    QuerySet<IntegerTypes> queryset;

    IntegerTypes test_obj{
        0,
        -9223372036854775807LL - 1,  // int64 min
        -32768,                       // short min
        4294967295U                   // uint max
    };

    auto insert_result = queryset.insert(test_obj);
    ASSERT_TRUE(insert_result.has_value());

    auto select_result = queryset.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& obj = select_result.value()[0];
    EXPECT_EQ(obj.int64_field, -9223372036854775807LL - 1);
    EXPECT_EQ(obj.short_field, -32768);
    EXPECT_EQ(obj.uint_field, 4294967295U);
}

// ===== FLOATING POINT TYPES TESTS =====

TEST_F(FloatingTypesTest, InsertAndSelectFloatingTypes) {
    QuerySet<FloatingTypes> queryset;

    FloatingTypes test_obj{
        0,
        3.14159265359,    // double
        2.71828f          // float
    };

    auto insert_result = queryset.insert(test_obj);
    ASSERT_TRUE(insert_result.has_value());

    auto select_result = queryset.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& obj = select_result.value()[0];
    EXPECT_DOUBLE_EQ(obj.double_field, 3.14159265359);
    EXPECT_FLOAT_EQ(obj.float_field, 2.71828f);
}

TEST_F(FloatingTypesTest, FloatingPointPrecision) {
    QuerySet<FloatingTypes> queryset;

    FloatingTypes test_obj{
        0,
        1.23456789012345,  // double with high precision
        9.8765f            // float
    };

    auto insert_result = queryset.insert(test_obj);
    ASSERT_TRUE(insert_result.has_value());

    auto select_result = queryset.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& obj = select_result.value()[0];
    EXPECT_NEAR(obj.double_field, 1.23456789012345, 1e-10);
    EXPECT_NEAR(obj.float_field, 9.8765f, 1e-4);
}

// ===== BOOLEAN TYPE TESTS =====

TEST_F(BooleanTypeTest, BooleanValues) {
    QuerySet<BooleanType> queryset;

    // Test with false
    BooleanType test_false{0, false, "test_false"};
    auto insert_result = queryset.insert(test_false);
    ASSERT_TRUE(insert_result.has_value());

    // Test with true
    BooleanType test_true{0, true, "test_true"};
    insert_result = queryset.insert(test_true);
    ASSERT_TRUE(insert_result.has_value());

    auto select_result = queryset.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& results = select_result.value();
    ASSERT_EQ(results.size(), 2);

    EXPECT_FALSE(results[0].bool_field);
    EXPECT_EQ(results[0].name, "test_false");

    EXPECT_TRUE(results[1].bool_field);
    EXPECT_EQ(results[1].name, "test_true");
}

// ===== OPTIONAL TYPES TESTS =====

TEST_F(OptionalTypesTest, OptionalWithValues) {
    QuerySet<OptionalTypes> queryset;

    OptionalTypes test_obj{
        0,
        std::optional<int>(123),
        std::optional<std::string>("optional text")
    };

    auto insert_result = queryset.insert(test_obj);
    ASSERT_TRUE(insert_result.has_value());

    auto select_result = queryset.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& obj = select_result.value()[0];

    ASSERT_TRUE(obj.opt_int_field.has_value());
    EXPECT_EQ(obj.opt_int_field.value(), 123);

    ASSERT_TRUE(obj.opt_string_field.has_value());
    EXPECT_EQ(obj.opt_string_field.value(), "optional text");
}

TEST_F(OptionalTypesTest, OptionalWithNullValues) {
    QuerySet<OptionalTypes> queryset;

    OptionalTypes test_obj{
        0,
        std::nullopt,
        std::nullopt
    };

    auto insert_result = queryset.insert(test_obj);
    ASSERT_TRUE(insert_result.has_value());

    auto select_result = queryset.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& obj = select_result.value()[0];

    EXPECT_FALSE(obj.opt_int_field.has_value());
    EXPECT_FALSE(obj.opt_string_field.has_value());
}

// ===== BLOB TYPE TESTS =====

TEST_F(BlobTypeTest, SmallBlob) {
    QuerySet<BlobType> queryset;

    BlobType test_obj{
        0,
        std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF},
        "small_blob"
    };

    auto insert_result = queryset.insert(test_obj);
    ASSERT_TRUE(insert_result.has_value());

    auto select_result = queryset.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& obj = select_result.value()[0];
    EXPECT_EQ(obj.blob_field, (std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));
    EXPECT_EQ(obj.name, "small_blob");
}

TEST_F(BlobTypeTest, LargeBlob) {
    QuerySet<BlobType> queryset;

    // Create 1KB of test data
    std::vector<uint8_t> large_blob(1024);
    for (size_t i = 0; i < large_blob.size(); ++i) {
        large_blob[i] = static_cast<uint8_t>(i % 256);
    }

    BlobType test_obj{0, large_blob, "large_blob"};

    auto insert_result = queryset.insert(test_obj);
    ASSERT_TRUE(insert_result.has_value());

    auto select_result = queryset.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& obj = select_result.value()[0];
    ASSERT_EQ(obj.blob_field.size(), 1024);

    for (size_t i = 0; i < obj.blob_field.size(); ++i) {
        EXPECT_EQ(obj.blob_field[i], static_cast<uint8_t>(i % 256))
            << "BLOB data mismatch at index " << i;
    }
}

TEST_F(BlobTypeTest, EmptyBlob) {
    QuerySet<BlobType> queryset;

    BlobType test_obj{0, std::vector<uint8_t>{}, "empty_blob"};

    auto insert_result = queryset.insert(test_obj);
    ASSERT_TRUE(insert_result.has_value());

    auto select_result = queryset.select();
    ASSERT_TRUE(select_result.has_value());

    const auto& obj = select_result.value()[0];
    EXPECT_TRUE(obj.blob_field.empty());
    EXPECT_EQ(obj.name, "empty_blob");
}
