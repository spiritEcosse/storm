/**
 * @file fuzz_where_string.cpp
 * @brief libFuzzer harness — fuzz string WHERE filter values.
 *
 * Exercises the runtime SQL binding layer for string comparisons:
 * ==, !=, LIKE. SQL errors are expected and caught;
 * only crashes or ASAN/UBSAN reports indicate real bugs.
 */

// NOLINTBEGIN(modernize-use-trailing-return-type,bugprone-unused-return-value,bugprone-empty-catch,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

#include <cstddef>
#include <cstdint>

import storm;

import <expected>;
import <memory>;
import <meta>;
import <string>;

#include "fuzz_models.h" // AFTER import storm;

extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/) {
    auto result = storm::QuerySet<FuzzModel>::set_default_connection(":memory:");
    if (!result.has_value()) {
        return 0;
    }
    const auto& conn = storm::QuerySet<FuzzModel>::get_default_connection();
    (void)conn->execute(fuzz_model_create_sql);
    // Seed one row so WHERE queries can find results
    (void)storm::QuerySet<FuzzModel>().insert(FuzzModel{.name = "seed", .value = 42}).execute();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::string input(reinterpret_cast<const char*>(data), size);

    try {
        // Equal
        (void)storm::QuerySet<FuzzModel>()
                .where(storm::orm::where::field<^^FuzzModel::name>() == input)
                .select()
                .execute();
        // Not equal
        (void)storm::QuerySet<FuzzModel>()
                .where(storm::orm::where::field<^^FuzzModel::name>() != input)
                .select()
                .execute();
        // LIKE
        (void)storm::QuerySet<FuzzModel>()
                .where(storm::orm::where::field<^^FuzzModel::name>().like(input))
                .select()
                .execute();
    } catch (...) {
        // SQL errors are expected — only crashes/ASAN hits are bugs
    }
    return 0;
}

// NOLINTEND(modernize-use-trailing-return-type,bugprone-unused-return-value,bugprone-empty-catch,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
