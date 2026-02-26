/**
 * @file fuzz_where_int.cpp
 * @brief libFuzzer harness — fuzz int/double WHERE filter values.
 *
 * Interprets raw fuzz bytes as int/double values and exercises
 * the runtime SQL binding layer for numeric comparisons:
 * ==, !=, >, <, IN, BETWEEN.
 */

// NOLINTBEGIN(modernize-use-trailing-return-type,bugprone-unused-return-value,bugprone-empty-catch,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

#include <cstddef>
#include <cstdint>
#include <cstring>

import storm;

import <expected>;
import <memory>;
import <meta>;

#include "fuzz_models.h" // AFTER import storm;

extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/) {
    auto result = storm::QuerySet<FuzzModel>::set_default_connection(":memory:");
    if (!result.has_value()) {
        return 0;
    }
    const auto& conn = storm::QuerySet<FuzzModel>::get_default_connection();
    (void)conn->execute(fuzz_model_create_sql);
    (void)storm::QuerySet<FuzzModel>().insert(FuzzModel{.name = "seed", .value = 42}).execute();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Extract two int values from the fuzz input (pad with zeros if short)
    int val1 = 0;
    int val2 = 0;

    if (size >= sizeof(int)) {
        std::memcpy(&val1, data, sizeof(int));
    }
    if (size >= 2 * sizeof(int)) {
        std::memcpy(&val2, data + sizeof(int), sizeof(int));
    }

    try {
        // Equal int
        (void)storm::QuerySet<FuzzModel>()
                .where(storm::orm::where::field<^^FuzzModel::value>() == val1)
                .select()
                .execute();
        // Not equal
        (void)storm::QuerySet<FuzzModel>()
                .where(storm::orm::where::field<^^FuzzModel::value>() != val1)
                .select()
                .execute();
        // Greater than
        (void)storm::QuerySet<FuzzModel>()
                .where(storm::orm::where::field<^^FuzzModel::value>() > val1)
                .select()
                .execute();
        // Less than
        (void)storm::QuerySet<FuzzModel>()
                .where(storm::orm::where::field<^^FuzzModel::value>() < val2)
                .select()
                .execute();
        // IN with two values
        (void)storm::QuerySet<FuzzModel>()
                .where(storm::orm::where::field<^^FuzzModel::value>().in(val1, val2))
                .select()
                .execute();
        // BETWEEN (ensure lower <= upper to form a valid range)
        const int lower = val1 < val2 ? val1 : val2;
        const int upper = val1 < val2 ? val2 : val1;
        (void)storm::QuerySet<FuzzModel>()
                .where(storm::orm::where::field<^^FuzzModel::value>().between(lower, upper))
                .select()
                .execute();
    } catch (...) {
        // SQL errors are expected — only crashes/ASAN hits are bugs
    }
    return 0;
}

// NOLINTEND(modernize-use-trailing-return-type,bugprone-unused-return-value,bugprone-empty-catch,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
