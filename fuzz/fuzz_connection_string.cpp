/**
 * @file fuzz_connection_string.cpp
 * @brief libFuzzer harness — fuzz malformed connection strings.
 *
 * Passes arbitrary byte sequences as the connection string to
 * set_default_connection(). The call must return an error (not crash)
 * for invalid inputs. A successful connection is immediately cleared.
 */

// NOLINTBEGIN(modernize-use-trailing-return-type,bugprone-unused-return-value,bugprone-empty-catch,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

#include <cstddef>
#include <cstdint>

import storm;

import <expected>;
import <string>;

#include "fuzz_models.h" // AFTER import storm;

extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/) {
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::string input(reinterpret_cast<const char*>(data), size);

    try {
        auto result = storm::QuerySet<FuzzModel>::set_default_connection(input);
        if (result.has_value()) {
            // Unexpected success — clear connection to avoid resource leak
            storm::QuerySet<FuzzModel>::clear_default_connection();
        }
        // Error return is the expected outcome for malformed strings
    } catch (...) {
        // Exceptions are acceptable — only crashes/ASAN hits are bugs
    }
    return 0;
}

// NOLINTEND(modernize-use-trailing-return-type,bugprone-unused-return-value,bugprone-empty-catch,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
