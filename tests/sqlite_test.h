#include <gtest/gtest.h>
#include <meta>

using namespace std::meta;

// Field attribute enum
enum class FieldAttr {
    primary,
    indexed,
    unique
};

// Test struct with various attributes
struct Point {
    int x;
    int id;
    [[=FieldAttr::primary]] int age;
    int y;
    [[=FieldAttr::indexed]] int z;
    int w;  // No attribute
};

consteval bool has_primary_attr(std::meta::info member) {
    // Test: annotation_of_type<FieldAttr>(member) - template approach
    // This actually works! It can access the function parameter properly
    auto field_attr = annotation_of_type<FieldAttr>(member);
    return field_attr.has_value() && field_attr.value() == FieldAttr::primary;
}

// CLEAN: Primary key finder using annotation_of_type (this is the best approach!)
consteval auto find_primary_key_clean() {
    for (std::meta::info member : nonstatic_data_members_of(^^Point, access_context::unchecked())) {
        // Clean approach: directly use annotation_of_type
        if (has_primary_attr(member)) {
            return identifier_of(member);  // Found primary key!
        }
    }
    return std::string_view{};
}

class ORMTest : public ::testing::Test {
};

TEST_F(ORMTest, PrimaryKeyTest) {
    constexpr auto clean_result = find_primary_key_clean();

    // Verify that the reflection correctly identifies "age" as the primary key field
    EXPECT_EQ(clean_result, "age") << "Expected to find 'age' field as primary key";
    EXPECT_FALSE(clean_result.empty()) << "Primary key field should be found";

    // Print for debugging
    printf("Clean approach (annotation_of_type): %.*s\n",
           static_cast<int>(clean_result.size()), clean_result.data());
}
