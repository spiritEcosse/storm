#include <gtest/gtest.h>
#include "SQLiteTest.h"

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);

    // Run all tests
    return RUN_ALL_TESTS();
}
