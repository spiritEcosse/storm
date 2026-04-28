#pragma once

// POD-only bridge between main.cpp (owns <benchmark/benchmark.h>) and
// register.cpp (does `import storm_*` and builds Storm benchmark closures).
// This header MUST NOT include benchmark.h or any storm module header so that
// main.cpp and register.cpp can keep their incompatible compile contexts —
// see the split rationale in benchmarks/CMakeLists.txt.

#include <cstdint>
#include <string>
#include <vector>

namespace storm::benchmark {

// Trampoline signature — opaque to main.cpp (it never inspects N or which
// Storm closure runs). Each entry's `run` is invoked once per iteration
// by the benchmark loop body in main.cpp; setup happens once per Range
// step via `setup`.
struct RegisteredBenchmark {
    std::string name;
    // setup(N): build dataset + Storm terminal for this Range step
    void (*setup)(int64_t n);
    // run(): single-iteration call inside `for (auto _ : state)`
    void (*run)();

    // Range parameters — main.cpp forwards these to gbench's Benchmark*
    // chain. Exactly one of (sized, !args.empty(), single Arg) applies:
    //   sized=true            → RangeMultiplier(range_multiplier)->Range(lo, hi)
    //   !args.empty()         → Args sweep (one ->Arg(N) per element); used
    //                            for non-power-of-RangeMultiplier sequences
    //                            like batch_standard {1,10,100,500,1000,...}
    //   sized=false, args={}  → single ->Arg(range_lo)
    bool sized;
    int64_t range_lo;
    int64_t range_hi;
    int range_multiplier;
    std::vector<int64_t> args;
};

// Populated by register.cpp (called once before BENCHMARK_MAIN).
auto registered_benchmarks() -> std::vector<RegisteredBenchmark> &;

// Process-wide DB init. Called from main.cpp before registration.
auto initialize_db() -> bool;

// Walk BENCHMARK_TESTS at compile time, push one entry per SELECT-family
// test into registered_benchmarks(). Defined in register.cpp.
auto build_registration_table() -> void;

} // namespace storm::benchmark
