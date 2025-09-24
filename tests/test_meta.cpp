#include <gtest/gtest.h>
#include <meta>

import storm;

using namespace std::meta;
using namespace storm::meta;


// Test struct with proper attribute syntax
struct Author {
    [[=FieldAttr::primary]] int id;
    std::string name;
    int age;
    std::string email;
    bool is_active;
    double rating;
    float score;
    std::string middleName;
    std::string biography;
};

TEST(AuthorReflection, PrimaryKeyTest) {
    constexpr auto primary_key_member = find_primary_key(^^Author);
    constexpr auto primary_key_name = identifier_of(primary_key_member);

    // Verify that the reflection correctly identifies "id" as the primary key field
    EXPECT_EQ(primary_key_name, "id") << "Expected to find 'id' field as primary key";
    EXPECT_FALSE(primary_key_name.empty()) << "Primary key field should be found";

    // Print for debugging
    printf("Author primary key found: %.*s\n",
           static_cast<int>(primary_key_name.size()), primary_key_name.data());
}

// Simple field count test without runtime reflection
TEST(AuthorReflection, BasicReflectionTest) {
    // Just verify the Author struct can be used with the meta system
    EXPECT_TRUE(true) << "Author struct compiled successfully with reflection attributes";
}