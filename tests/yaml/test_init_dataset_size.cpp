#include <gtest/gtest.h>

import std;
import storm_benchmark_parser;

// Issue #72: the benchmark config key `dataset_size` was renamed to
// `init_dataset_size` for clarity (it is the size of the dataset seeded during
// setup, not the size being measured). The consteval config parser accepts the
// new name as the preferred key and keeps the legacy `dataset_size` as a
// backward-compatible alias, mirroring the existing `test_category`/`category`
// dual-accept precedent in the same parser.

namespace {

    constexpr auto parse_size(std::string_view json) -> int {
        std::size_t pos = 0;
        return storm::benchmark::parse_test_object(json, pos).dataset_size;
    }

    // New preferred key populates BenchmarkTest::dataset_size.
    TEST(InitDatasetSize, NewKeyIsParsed) {
        constexpr int v = parse_size(R"({"init_dataset_size": 4242})");
        EXPECT_EQ(v, 4242);
    }

    // Legacy key still parses (backward compatible).
    TEST(InitDatasetSize, LegacyKeyStillParsed) {
        constexpr int v = parse_size(R"({"dataset_size": 777})");
        EXPECT_EQ(v, 777);
    }

    // Absent key keeps the struct default.
    TEST(InitDatasetSize, DefaultWhenAbsent) {
        constexpr int v = parse_size(R"({"iterations": 5})");
        EXPECT_EQ(v, 10000);
    }

} // namespace
