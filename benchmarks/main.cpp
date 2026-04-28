/**
 * Storm ORM benchmarks — Google Benchmark entry point (Issue #235).
 *
 * Phase 1: minimal proof that Google Benchmark v1.9.5 + custom Clang +
 * Storm's module FILE_SET link cleanly. One noop fixture only. Phase 2
 * starts porting the YAML-driven catalog into BENCHMARK_TEMPLATE_F
 * registrations consuming the existing parser/registry/sizes modules.
 */

#include <benchmark/benchmark.h>

static void BM_Noop(benchmark::State& state) {
    int x = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(x);
    }
}
BENCHMARK(BM_Noop);

BENCHMARK_MAIN();
