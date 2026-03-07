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
import <span>;

#include "test_models.h"

using namespace storm;

// ===== INTEGER TYPES TESTS =====

template <typename ConnType> class IntTypesInsertUpdateTest : public StormTestFixture<ExtendedTypes, ConnType> {};

TYPED_TEST_SUITE(IntTypesInsertUpdateTest, DatabaseTypes);

TYPED_TEST(IntTypesInsertUpdateTest, InsertSingleIntTypes) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .big_num = 9223372036854775807LL, .ll_signed = 32767LL};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    ASSERT_EQ(selected.value().size(), 1);
    EXPECT_EQ(selected.value().begin()->big_num, 9223372036854775807LL);
    EXPECT_EQ(selected.value().begin()->ll_signed, 32767LL);
}

TYPED_TEST(IntTypesInsertUpdateTest, InsertBatchIntTypes) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    std::vector<ExtendedTypes>         batch = {
            {.id = 0, .big_num = 100LL, .ll_signed = 10LL},
            {.id = 0, .big_num = 200LL, .ll_signed = 20LL},
            {.id = 0, .big_num = 300LL, .ll_signed = 30LL},
    };

    auto result = qs.insert(batch).execute();
    ASSERT_TRUE(result.has_value());

    // Batch insert returns void, verify via SELECT
    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
    EXPECT_EQ(selected.value().begin()->big_num, 100LL);
}

TYPED_TEST(IntTypesInsertUpdateTest, UpdateSingleIntTypes) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .big_num = 100LL, .ll_signed = 10LL};

    auto insert_result = qs.insert(obj).execute();
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    // Verify INSERT worked
    auto check1 = qs.select().execute();
    ASSERT_TRUE(check1.has_value());
    auto it1 = check1.value().begin();
    EXPECT_EQ(it1->big_num, 100LL);
    EXPECT_EQ(it1->ll_signed, 10LL);

    // Update with the returned ID
    ExtendedTypes const updated{.id = static_cast<int>(id), .big_num = 999LL, .ll_signed = 99LL};
    auto                update_result = qs.update(updated).execute();
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    auto it2 = selected.value().begin();
    EXPECT_EQ(it2->big_num, 999LL);
    EXPECT_EQ(it2->ll_signed, 99LL);
}

TYPED_TEST(IntTypesInsertUpdateTest, UpdateBatchIntTypes) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    std::vector<ExtendedTypes>         batch = {
            {.id = 1, .big_num = 100LL, .ll_signed = 10LL},
            {.id = 2, .big_num = 200LL, .ll_signed = 20LL},
            {.id = 3, .big_num = 300LL, .ll_signed = 30LL},
    };

    auto insert_result = qs.insert(batch).execute();
    ASSERT_TRUE(insert_result.has_value());

    // Update all
    std::vector<ExtendedTypes> updates = {
            {.id = 1, .big_num = 111LL, .ll_signed = 11LL},
            {.id = 2, .big_num = 222LL, .ll_signed = 22LL},
            {.id = 3, .big_num = 333LL, .ll_signed = 33LL},
    };
    auto update_result = qs.update(std::span<const ExtendedTypes>(updates)).execute();
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    auto it = selected.value().begin();
    EXPECT_EQ(it->big_num, 111LL);
    ++it;
    EXPECT_EQ(it->big_num, 222LL);
    ++it;
    EXPECT_EQ(it->big_num, 333LL);
}

// ===== FLOATING POINT TYPES TESTS =====

template <typename ConnType> class FloatTypesInsertUpdateTest : public StormTestFixture<ExtendedTypes, ConnType> {};

TYPED_TEST_SUITE(FloatTypesInsertUpdateTest, DatabaseTypes);

TYPED_TEST(FloatTypesInsertUpdateTest, InsertSingleFloatTypes) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .precise = std::numbers::pi, .approx = std::numbers::e_v<float>};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_NEAR(selected.value().begin()->precise, std::numbers::pi, 1e-10);
    EXPECT_NEAR(selected.value().begin()->approx, std::numbers::e_v<float>, 1e-6);
}

TYPED_TEST(FloatTypesInsertUpdateTest, UpdateSingleFloatTypes) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .precise = 1.0, .approx = 1.0F};

    auto insert_result = qs.insert(obj).execute();
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    ExtendedTypes const updated{
            .id = static_cast<int>(id), .precise = std::numbers::e, .approx = std::numbers::pi_v<float>
    };
    auto update_result = qs.update(updated).execute();
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_NEAR(selected.value().begin()->precise, std::numbers::e, 1e-10);
    EXPECT_NEAR(selected.value().begin()->approx, std::numbers::pi_v<float>, 1e-6);
}

// ===== BOOLEAN AND STRING TYPES TESTS =====

template <typename ConnType> class MixedTypesInsertUpdateTest : public StormTestFixture<Person, ConnType> {};

TYPED_TEST_SUITE(MixedTypesInsertUpdateTest, DatabaseTypes);

TYPED_TEST(MixedTypesInsertUpdateTest, InsertBooleanTrue) {
    QuerySet<Person, TypeParam> qs;
    Person const                obj{.id = 0, .name = "active_user", .is_active = true};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_TRUE(selected.value().begin()->is_active);
    EXPECT_EQ(selected.value().begin()->name, "active_user");
}

TYPED_TEST(MixedTypesInsertUpdateTest, InsertBooleanFalse) {
    QuerySet<Person, TypeParam> qs;
    Person const                obj{.id = 0, .name = "inactive_user", .is_active = false};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->is_active);
    EXPECT_EQ(selected.value().begin()->name, "inactive_user");
}

TYPED_TEST(MixedTypesInsertUpdateTest, UpdateBooleanAndString) {
    QuerySet<Person, TypeParam> qs;
    Person const                obj{.id = 0, .name = "old_name", .is_active = false};

    auto insert_result = qs.insert(obj).execute();
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    Person const updated{.id = static_cast<int>(id), .name = "new_name", .is_active = true};
    auto         update_result = qs.update(updated).execute();
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_TRUE(selected.value().begin()->is_active);
    EXPECT_EQ(selected.value().begin()->name, "new_name");
}

TYPED_TEST(MixedTypesInsertUpdateTest, InsertBatchMixedTypes) {
    QuerySet<Person, TypeParam> qs;
    std::vector<Person>         batch = {
            {.id = 1, .name = "user1", .is_active = true},
            {.id = 2, .name = "user2", .is_active = false},
            {.id = 3, .name = "user3", .is_active = true},
    };

    auto result = qs.insert(batch).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
    auto it = selected.value().begin();
    EXPECT_TRUE(it->is_active);
    ++it;
    EXPECT_FALSE(it->is_active);
    ++it;
    EXPECT_EQ(it->name, "user3");
}

// ===== OPTIONAL TYPES TESTS =====

template <typename ConnType> class OptTypesInsertUpdateTest : public StormTestFixture<Person, ConnType> {};

TYPED_TEST_SUITE(OptTypesInsertUpdateTest, DatabaseTypes);

TYPED_TEST(OptTypesInsertUpdateTest, InsertWithValues) {
    QuerySet<Person, TypeParam> qs;
    Person const                obj{.id = 0, .name = "with_value", .score = std::optional<int>(42)};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(selected.value().begin()->score.has_value());
    EXPECT_EQ(selected.value().begin()->score.value(), 42);
    EXPECT_EQ(selected.value().begin()->name, "with_value");
}

TYPED_TEST(OptTypesInsertUpdateTest, InsertWithNull) {
    QuerySet<Person, TypeParam> qs;
    Person const                obj{.id = 0, .name = "null_value", .score = std::nullopt};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->score.has_value());
    EXPECT_EQ(selected.value().begin()->name, "null_value");
}

TYPED_TEST(OptTypesInsertUpdateTest, UpdateFromValueToNull) {
    QuerySet<Person, TypeParam> qs;
    Person const                obj{.id = 0, .name = "original", .score = std::optional<int>(100)};

    auto insert_result = qs.insert(obj).execute();
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    // Update to NULL
    Person const updated{.id = static_cast<int>(id), .name = "updated_to_null", .score = std::nullopt};
    auto         update_result = qs.update(updated).execute();
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->score.has_value());
    EXPECT_EQ(selected.value().begin()->name, "updated_to_null");
}

TYPED_TEST(OptTypesInsertUpdateTest, UpdateFromNullToValue) {
    QuerySet<Person, TypeParam> qs;
    Person const                obj{.id = 0, .name = "null_start", .score = std::nullopt};

    auto insert_result = qs.insert(obj).execute();
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    // Update from NULL to value
    Person const updated{.id = static_cast<int>(id), .name = "updated_to_value", .score = std::optional<int>(999)};
    auto         update_result = qs.update(updated).execute();
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(selected.value().begin()->score.has_value());
    EXPECT_EQ(selected.value().begin()->score.value(), 999);
    EXPECT_EQ(selected.value().begin()->name, "updated_to_value");
}

TYPED_TEST(OptTypesInsertUpdateTest, InsertBatchMixedNulls) {
    QuerySet<Person, TypeParam> qs;
    std::vector<Person>         batch = {
            {.id = 1, .name = "has_value", .score = std::optional<int>(1)},
            {.id = 2, .name = "is_null", .score = std::nullopt},
            {.id = 3, .name = "has_value2", .score = std::optional<int>(3)},
            {.id = 4, .name = "is_null2", .score = std::nullopt},
    };

    auto result = qs.insert(batch).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 4);

    // Verify mixed NULL/value pattern
    auto it = selected.value().begin();
    EXPECT_TRUE(it->score.has_value());
    EXPECT_EQ(it->score.value(), 1);
    ++it;
    EXPECT_FALSE(it->score.has_value());
    ++it;
    EXPECT_TRUE(it->score.has_value());
    EXPECT_EQ(it->score.value(), 3);
    ++it;
    EXPECT_FALSE(it->score.has_value());
}

// ===== BLOB TYPES TESTS =====

template <typename ConnType> class DataTypesInsertUpdateTest : public StormTestFixture<Person, ConnType> {};

TYPED_TEST_SUITE(DataTypesInsertUpdateTest, DatabaseTypes);

TYPED_TEST(DataTypesInsertUpdateTest, InsertSmallBlob) {
    QuerySet<Person, TypeParam> qs;
    Person const                obj{.id = 0, .name = "test_blob", .avatar = {0xDE, 0xAD, 0xBE, 0xEF}};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->avatar, (std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));
    EXPECT_EQ(selected.value().begin()->name, "test_blob");
}

TYPED_TEST(DataTypesInsertUpdateTest, InsertLargeBlob) {
    QuerySet<Person, TypeParam> qs;

    std::vector<uint8_t> large_data(1024);
    for (size_t i = 0; i < large_data.size(); ++i) {
        large_data[i] = static_cast<uint8_t>(i % 256);
    }

    Person const obj{.id = 0, .name = "large", .avatar = large_data};
    auto         result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    auto it = selected.value().begin();
    ASSERT_EQ(it->avatar.size(), 1024);

    for (size_t i = 0; i < 1024; ++i) {
        EXPECT_EQ(it->avatar[i], static_cast<uint8_t>(i % 256));
    }
}

TYPED_TEST(DataTypesInsertUpdateTest, InsertEmptyBlob) {
    QuerySet<Person, TypeParam> qs;
    Person const                obj{.id = 0, .name = "empty", .avatar = {}};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_TRUE(selected.value().begin()->avatar.empty());
}

TYPED_TEST(DataTypesInsertUpdateTest, UpdateBlob) {
    QuerySet<Person, TypeParam> qs;
    Person const                obj{.id = 0, .name = "original", .avatar = {0x01, 0x02}};

    auto insert_result = qs.insert(obj).execute();
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    // Update with different blob
    Person const updated{.id = static_cast<int>(id), .name = "updated", .avatar = {0xFF, 0xEE, 0xDD}};
    auto         update_result = qs.update(updated).execute();
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->avatar, (std::vector<uint8_t>{0xFF, 0xEE, 0xDD}));
    EXPECT_EQ(selected.value().begin()->name, "updated");
}

TYPED_TEST(DataTypesInsertUpdateTest, InsertBatchBlobs) {
    QuerySet<Person, TypeParam> qs;
    std::vector<Person>         batch = {
            {.id = 1, .name = "blob1", .avatar = {0x01}},
            {.id = 2, .name = "blob2", .avatar = {0x02, 0x03}},
            {.id = 3, .name = "blob3", .avatar = {0x04, 0x05, 0x06}},
    };

    auto result = qs.insert(batch).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
    auto it = selected.value().begin();
    EXPECT_EQ(it->avatar.size(), 1);
    ++it;
    EXPECT_EQ(it->avatar.size(), 2);
    ++it;
    EXPECT_EQ(it->avatar.size(), 3);
}

// ===== EXTREME VALUE TESTS =====

TYPED_TEST(IntTypesInsertUpdateTest, ExtremeIntegerValues) {
    QuerySet<ExtendedTypes, TypeParam> qs;

    // Min values
    ExtendedTypes const min_obj{.id = 0, .big_num = -9223372036854775807LL - 1, .ll_signed = -32768LL};
    auto                result = qs.insert(min_obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    auto it = selected.value().begin();
    EXPECT_EQ(it->big_num, -9223372036854775807LL - 1);
    EXPECT_EQ(it->ll_signed, -32768LL);
}

TYPED_TEST(FloatTypesInsertUpdateTest, SpecialFloatValues) {
    QuerySet<ExtendedTypes, TypeParam> qs;

    // Zero, negative, very small
    ExtendedTypes const obj{.id = 0, .precise = 0.0, .approx = -0.0F};
    auto                result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_DOUBLE_EQ(selected.value().begin()->precise, 0.0);
}

// ===== INSERT OPTIONS TESTS =====

template <typename ConnType> class InsertOptionsTest : public StormTestFixture<ExtendedTypes, ConnType> {};

TYPED_TEST_SUITE(InsertOptionsTest, DatabaseTypes);

TYPED_TEST(InsertOptionsTest, BatchInsertReturnsVoid) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    std::vector<ExtendedTypes>         batch = {
            {.id = 0, .big_num = 100LL, .ll_signed = 10LL},
            {.id = 0, .big_num = 200LL, .ll_signed = 20LL},
            {.id = 0, .big_num = 300LL, .ll_signed = 30LL},
    };

    // Batch insert returns void (no IDs - SQLite's last_insert_rowid is unreliable for batch)
    auto result = qs.insert(batch).execute();
    ASSERT_TRUE(result.has_value());

    // Verify data was inserted by selecting
    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
}

TYPED_TEST(InsertOptionsTest, InsertWithCustomBatchSize) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    std::vector<ExtendedTypes>         batch;

    // Create 100 objects
    batch.reserve(100);
    for (int i = 0; i < 100; ++i) {
        batch.emplace_back(
                ExtendedTypes{.id = 0, .big_num = static_cast<int64_t>(i) * 10, .ll_signed = static_cast<long long>(i)}
        );
    }

    // Use batch_size of 10
    auto result = qs.insert(batch, {{.batch_size = 10}}).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 100);
}

TYPED_TEST(InsertOptionsTest, InsertBatchSizeCappedToMax) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    std::vector<ExtendedTypes>         batch = {
            {.id = 0, .big_num = 100LL, .ll_signed = 10LL},
            {.id = 0, .big_num = 200LL, .ll_signed = 20LL},
    };

    // ExtendedTypes has 8 non-PK fields, so max = 999/8 = 124
    // Request batch_size of 1000, should be capped to 124
    auto result = qs.insert(batch, {{.batch_size = 1000}}).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 2);
}

TYPED_TEST(InsertOptionsTest, SingleInsertReturnsId) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .big_num = 999LL, .ll_signed = 99LL};

    // Single insert still returns the auto-generated ID
    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1);

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 1);
    EXPECT_EQ(selected.value().begin()->big_num, 999LL);
}

TYPED_TEST(InsertOptionsTest, LargeBatchWithCustomChunkSize) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    std::vector<ExtendedTypes>         batch;

    // Create 1000 objects
    batch.reserve(1000);
    for (int i = 0; i < 1000; ++i) {
        batch.emplace_back(
                ExtendedTypes{.id = 0, .big_num = static_cast<int64_t>(i), .ll_signed = static_cast<long long>(i % 100)}
        );
    }

    // Use small batch_size to force multiple chunks
    auto result = qs.insert(batch, {{.batch_size = 50}}).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 1000);
}

TYPED_TEST(InsertOptionsTest, OptionsWithOnlyBatchSize) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    std::vector<ExtendedTypes>         batch = {
            {.id = 0, .big_num = 100LL, .ll_signed = 10LL},
            {.id = 0, .big_num = 200LL, .ll_signed = 20LL},
    };

    // Only specify batch_size
    auto result = qs.insert(batch, {{.batch_size = 1}}).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 2);
}

// ===== UNSIGNED INTEGER TYPES TESTS =====

template <typename ConnType> class UnsignedTypesInsertUpdateTest : public StormTestFixture<ExtendedTypes, ConnType> {};

TYPED_TEST_SUITE(UnsignedTypesInsertUpdateTest, DatabaseTypes);

TYPED_TEST(UnsignedTypesInsertUpdateTest, InsertUnsignedValues) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .u_int = 4294967295U};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    ASSERT_EQ(selected.value().size(), 1);
    EXPECT_EQ(selected.value().begin()->u_int, 4294967295U);
}

TYPED_TEST(UnsignedTypesInsertUpdateTest, InsertBatchUnsigned) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    std::vector<ExtendedTypes>         batch = {
            {.id = 0, .u_int = 100U},
            {.id = 0, .u_int = 200U},
            {.id = 0, .u_int = 300U},
    };

    auto result = qs.insert(batch).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
}

TYPED_TEST(UnsignedTypesInsertUpdateTest, UpdateUnsignedValues) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .u_int = 100U};

    auto insert_result = qs.insert(obj).execute();
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    ExtendedTypes const updated{.id = static_cast<int>(id), .u_int = 999U};
    auto                update_result = qs.update(updated).execute();
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->u_int, 999U);
}

// ===== LONG LONG TYPES TESTS =====

template <typename ConnType> class LongLongTypesInsertUpdateTest : public StormTestFixture<ExtendedTypes, ConnType> {};

TYPED_TEST_SUITE(LongLongTypesInsertUpdateTest, DatabaseTypes);

TYPED_TEST(LongLongTypesInsertUpdateTest, InsertLongLongValues) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .ll_signed = 9223372036854775807LL};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    ASSERT_EQ(selected.value().size(), 1);
    EXPECT_EQ(selected.value().begin()->ll_signed, 9223372036854775807LL);
}

TYPED_TEST(LongLongTypesInsertUpdateTest, InsertNegativeLongLong) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .ll_signed = -9223372036854775807LL};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->ll_signed, -9223372036854775807LL);
}

TYPED_TEST(LongLongTypesInsertUpdateTest, UpdateLongLongValues) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .ll_signed = 100LL};

    auto insert_result = qs.insert(obj).execute();
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    ExtendedTypes const updated{.id = static_cast<int>(id), .ll_signed = -999LL};
    auto                update_result = qs.update(updated).execute();
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->ll_signed, -999LL);
}

// Note: WHERE clause tests with unsigned types are not included because the
// ExpressionVariant doesn't support unsigned types directly. INSERT/UPDATE work
// with unsigned types through bind_parameter_value which casts them appropriately.

// ===== ERROR HANDLING TESTS =====

// Test Error struct accessors for code coverage
TEST(ErrorHandlingTest, ErrorAccessors) {
    // Try to open a database in a non-existent directory (should fail)
    auto result = QuerySet<ExtendedTypes>::set_default_connection("/nonexistent/path/database.db");

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
    auto conn_result = QuerySet<ExtendedTypes>::set_default_connection(storm::test::get_connection_string());
    ASSERT_TRUE(conn_result.has_value());

    // Try to select from non-existent table
    QuerySet<ExtendedTypes> qs;
    auto                    select_result = qs.select().execute();

    // Should fail because table doesn't exist
    ASSERT_FALSE(select_result.has_value()) << "Should fail to select from non-existent table";

    // Test error accessors
    const auto& error = select_result.error();
    EXPECT_NE(error.code(), 0);
    EXPECT_FALSE(error.message().empty());

    QuerySet<ExtendedTypes>::clear_default_connection();
}

// =============================================================================
// Additional Optional Type Coverage (from test_coverage_gaps.cpp)
// =============================================================================

// NOLINTBEGIN(readability-identifier-length,readability-uppercase-literal-suffix,modernize-use-std-numbers)

template <typename ConnType> class OptionalTypesTest : public StormTestFixture<ExtendedTypes, ConnType> {};

TYPED_TEST_SUITE(OptionalTypesTest, DatabaseTypes);

TYPED_TEST(OptionalTypesTest, OptionalDoubleWithValue) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .opt_double = 3.14159, .label = "pi"};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(selected.value().begin()->opt_double.has_value());
    EXPECT_NEAR(selected.value().begin()->opt_double.value(), 3.14159, 0.0001);
}

TYPED_TEST(OptionalTypesTest, OptionalDoubleNull) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .opt_double = std::nullopt, .label = "null_double"};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->opt_double.has_value());
}

TYPED_TEST(OptionalTypesTest, OptionalDoubleBatch) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    std::vector<ExtendedTypes>         batch = {
            {.id = 0, .opt_double = 1.1, .label = "first"},
            {.id = 0, .opt_double = std::nullopt, .label = "second"},
            {.id = 0, .opt_double = 2.2, .label = "third"},
            {.id = 0, .opt_double = std::nullopt, .label = "fourth"},
    };

    auto result = qs.insert(std::span<const ExtendedTypes>(batch)).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 4);

    auto it = selected.value().begin();
    EXPECT_TRUE(it->opt_double.has_value());
    ++it;
    EXPECT_FALSE(it->opt_double.has_value());
    ++it;
    EXPECT_TRUE(it->opt_double.has_value());
    ++it;
    EXPECT_FALSE(it->opt_double.has_value());
}

TYPED_TEST(OptionalTypesTest, OptionalInt64WithValue) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .opt_int64 = 9223372036854775807LL, .label = "max_int64"};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(selected.value().begin()->opt_int64.has_value());
    EXPECT_EQ(selected.value().begin()->opt_int64.value(), 9223372036854775807LL);
}

TYPED_TEST(OptionalTypesTest, OptionalInt64Null) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .opt_int64 = std::nullopt, .label = "null_int64"};

    auto result = qs.insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->opt_int64.has_value());
}

TYPED_TEST(OptionalTypesTest, UpdateOptionalDoubleToNull) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .opt_double = 100.5, .label = "to_null"};

    auto insert_result = qs.insert(obj).execute();
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    ExtendedTypes const updated{.id = static_cast<int>(id), .opt_double = std::nullopt, .label = "now_null"};
    auto                update_result = qs.update(updated).execute();
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_FALSE(selected.value().begin()->opt_double.has_value());
}

TYPED_TEST(OptionalTypesTest, UpdateOptionalInt64FromNull) {
    QuerySet<ExtendedTypes, TypeParam> qs;
    ExtendedTypes const                obj{.id = 0, .opt_int64 = std::nullopt, .label = "from_null"};

    auto insert_result = qs.insert(obj).execute();
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    ExtendedTypes const updated{.id = static_cast<int>(id), .opt_int64 = 42LL, .label = "now_has_value"};
    auto                update_result = qs.update(updated).execute();
    ASSERT_TRUE(update_result.has_value());

    auto selected = qs.select().execute();
    ASSERT_TRUE(selected.has_value());
    ASSERT_TRUE(selected.value().begin()->opt_int64.has_value());
    EXPECT_EQ(selected.value().begin()->opt_int64.value(), 42LL);
}

// =============================================================================
// Unsigned Integer Type Coverage (from test_coverage_gaps.cpp)
// =============================================================================

// NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
// NOLINTBEGIN(misc-const-correctness)

template <typename ConnType> class UnsignedTypesTest : public StormTestFixture<ExtendedTypes, ConnType> {
  protected:
    auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
        StormTestFixture<ExtendedTypes, ConnType>::on_setup(conn);
        if (this->HasFatalFailure())
            return;
        qs = std::make_unique<QuerySet<ExtendedTypes, ConnType>>();
    }

    auto TearDown() -> void override {
        qs = nullptr;
        StormTestFixture<ExtendedTypes, ConnType>::TearDown();
    }

    std::unique_ptr<QuerySet<ExtendedTypes, ConnType>> qs;
};

TYPED_TEST_SUITE(UnsignedTypesTest, DatabaseTypes);

TYPED_TEST(UnsignedTypesTest, InsertMaxValues) {
    ExtendedTypes const obj{
            .id    = 0,
            .u_int = std::numeric_limits<unsigned int>::max(),
    };

    auto result = this->qs->insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = this->qs->select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->u_int, std::numeric_limits<unsigned int>::max());
}

TYPED_TEST(UnsignedTypesTest, InsertZeroValues) {
    ExtendedTypes const obj{.id = 0, .u_int = 0};

    auto result = this->qs->insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = this->qs->select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->u_int, 0u);
}

TYPED_TEST(UnsignedTypesTest, BatchUnsignedTypes) {
    std::vector<ExtendedTypes> batch = {
            {.id = 0, .u_int = 100},
            {.id = 0, .u_int = 200},
            {.id = 0, .u_int = 300},
    };

    auto result = this->qs->insert(std::span<const ExtendedTypes>(batch)).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = this->qs->select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
}

TYPED_TEST(UnsignedTypesTest, UpdateUnsignedTypes) {
    ExtendedTypes const obj{.id = 0, .u_int = 100};

    auto insert_result = this->qs->insert(obj).execute();
    ASSERT_TRUE(insert_result.has_value());
    int64_t const id = insert_result.value();

    ExtendedTypes const updated{.id = static_cast<int>(id), .u_int = 999};
    auto                update_result = this->qs->update(updated).execute();
    ASSERT_TRUE(update_result.has_value());

    auto selected = this->qs->select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().begin()->u_int, 999u);
}

// =============================================================================
// Float Type Coverage (from test_coverage_gaps.cpp)
// =============================================================================

template <typename ConnType> class FloatTypeTest : public StormTestFixture<ExtendedTypes, ConnType> {
  protected:
    auto on_setup(const std::shared_ptr<ConnType>& conn) -> void override {
        StormTestFixture<ExtendedTypes, ConnType>::on_setup(conn);
        if (this->HasFatalFailure())
            return;
        qs = std::make_unique<QuerySet<ExtendedTypes, ConnType>>();
    }

    auto TearDown() -> void override {
        qs = nullptr;
        StormTestFixture<ExtendedTypes, ConnType>::TearDown();
    }

    std::unique_ptr<QuerySet<ExtendedTypes, ConnType>> qs;
};

TYPED_TEST_SUITE(FloatTypeTest, DatabaseTypes);

TYPED_TEST(FloatTypeTest, InsertFloatValue) {
    ExtendedTypes const obj{.id = 0, .approx = 3.14159f, .label = "pi"};

    auto result = this->qs->insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = this->qs->select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_NEAR(selected.value().begin()->approx, 3.14159f, 0.0001f);
}

TYPED_TEST(FloatTypeTest, InsertNegativeFloat) {
    ExtendedTypes const obj{.id = 0, .approx = -123.456f, .label = "negative"};

    auto result = this->qs->insert(obj).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = this->qs->select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_NEAR(selected.value().begin()->approx, -123.456f, 0.001f);
}

TYPED_TEST(FloatTypeTest, BatchFloatValues) {
    std::vector<ExtendedTypes> batch = {
            {.id = 0, .approx = 1.1f, .label = "first"},
            {.id = 0, .approx = 2.2f, .label = "second"},
            {.id = 0, .approx = 3.3f, .label = "third"},
    };

    auto result = this->qs->insert(std::span<const ExtendedTypes>(batch)).execute();
    ASSERT_TRUE(result.has_value());

    auto selected = this->qs->select().execute();
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value().size(), 3);
}

// NOLINTEND(misc-const-correctness)
// NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
// NOLINTEND(readability-identifier-length,readability-uppercase-literal-suffix,modernize-use-std-numbers)

// NOLINTEND(misc-use-internal-linkage,modernize-use-trailing-return-type,readability-named-parameter,readability-convert-member-functions-to-static)
