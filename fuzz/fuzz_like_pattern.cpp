/**
 * @file fuzz_like_pattern.cpp
 * @brief libFuzzer harness — fuzz LIKE pattern strings with adversarial input.
 *
 * Exercises the LIKE binding path with inputs that contain null bytes,
 * SQLite wildcards (%,_), quote characters, and unicode. SQL errors are
 * expected and caught; only crashes/ASAN hits are bugs.
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
    (void)storm::QuerySet<FuzzModel>().insert(FuzzModel{.name = "seed", .value = 42}).execute();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::string pattern(reinterpret_cast<const char*>(data), size);

    try {
        (void)storm::QuerySet<FuzzModel>()
                .where(storm::orm::where::field<^^FuzzModel::name>().like(pattern))
                .select()
                .execute();
    } catch (...) {
        // SQL errors are expected — only crashes/ASAN hits are bugs
    }
    return 0;
}

// NOLINTEND(modernize-use-trailing-return-type,bugprone-unused-return-value,bugprone-empty-catch,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
