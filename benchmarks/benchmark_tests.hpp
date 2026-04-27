#pragma once

// Compile-time `BENCHMARK_TESTS` array — materialized locally per-TU.
//
// Why is this not in the storm_benchmark_parser module?
// ------------------------------------------------------
// Putting a `consteval` function whose body contains a 50KB `#embed` literal
// (and the resulting `inline constexpr` array initializer) into the module
// purview triggers a clang PCM deserialization bug ("declaration ID
// out-of-range for AST file") when any TU imports the module and instantiates
// the call. The parser primitives (parse_int, parse_string, parse_tests<N>,
// count_tests, ...) serialize into the PCM fine — only the `consteval`
// `#embed` driver is affected.
//
// Keeping `load_benchmark_tests()` + `BENCHMARK_TESTS` in this textual header
// (currently included only by runner.hpp) sidesteps the bug while preserving
// the rest of the parser's module conversion.

#include <array>
#include <cstddef>
#include <string_view>

import storm_benchmark_schema; // BenchmarkTest
import storm_benchmark_parser; // parse_tests<>, count_tests

namespace storm::benchmark {

    consteval auto load_benchmark_tests() {
        static constexpr const char json_data[] = {
#embed "tests/benchmark_tests.json"
                , '\0'
        };
        constexpr std::string_view json_str(json_data);
        constexpr size_t           test_count = count_tests(json_str);
        return parse_tests<test_count>(json_str);
    }

    inline constexpr auto BENCHMARK_TESTS = load_benchmark_tests();

} // namespace storm::benchmark
