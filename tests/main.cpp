#include <gtest/gtest.h>

auto main(int argc, char** argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);

    // Run all tests
    return RUN_ALL_TESTS();
}
