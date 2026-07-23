#include <gtest/gtest.h>

import storm;
import std;

TEST(StitchKeyTest, SingleInt64PartEqualsItself) {
    storm::orm::utilities::StitchKey key_a;
    key_a.append_int64(42);
    storm::orm::utilities::StitchKey key_b;
    key_b.append_int64(42);
    EXPECT_EQ(key_a, key_b);
}

TEST(StitchKeyTest, DifferentInt64PartsAreNotEqual) {
    storm::orm::utilities::StitchKey key_a;
    key_a.append_int64(42);
    storm::orm::utilities::StitchKey key_b;
    key_b.append_int64(43);
    EXPECT_NE(key_a, key_b);
}

TEST(StitchKeyTest, TwoPartOrderMatters) {
    storm::orm::utilities::StitchKey key_a;
    key_a.append_int64(1);
    key_a.append_int64(2);
    storm::orm::utilities::StitchKey key_b;
    key_b.append_int64(2);
    key_b.append_int64(1);
    EXPECT_NE(key_a, key_b); // (1,2) is a different key from (2,1)
}

TEST(StitchKeyTest, ThreePartCompositeKeyRoundTrips) {
    storm::orm::utilities::StitchKey key_a;
    key_a.append_int64(7);
    key_a.append_string("sku-123");
    key_a.append_int64(2026);
    storm::orm::utilities::StitchKey key_b;
    key_b.append_int64(7);
    key_b.append_string("sku-123");
    key_b.append_int64(2026);
    EXPECT_EQ(key_a, key_b);
}

TEST(StitchKeyTest, HashableInUnorderedMap) {
    std::unordered_map<storm::orm::utilities::StitchKey, int> map;
    storm::orm::utilities::StitchKey                          key;
    key.append_int64(99);
    map[key] = 7;
    storm::orm::utilities::StitchKey lookup;
    lookup.append_int64(99);
    ASSERT_TRUE(map.contains(lookup));
    EXPECT_EQ(map.at(lookup), 7);
}

TEST(StitchKeyTest, StringPartDifferentValuesHashDifferently) {
    storm::orm::utilities::StitchKey key_a;
    key_a.append_int64(1);
    key_a.append_string("alice-warehouse");
    storm::orm::utilities::StitchKey key_b;
    key_b.append_int64(1);
    key_b.append_string("bob-warehouse");
    EXPECT_NE(key_a, key_b);
}
