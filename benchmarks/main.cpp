/**
 * Storm ORM benchmarks — Google Benchmark entry point (Issue #235 — Phase 2).
 *
 * Slim gbench shim: owns <benchmark/benchmark.h>, builds the registration
 * table from register.cpp, then drives RunSpecifiedBenchmarks.
 *
 * No `import` statements here — that's strictly the job of register.cpp.
 * Mixing both sides in one TU diverges this TU's PCM-cache hash from
 * libstorm.a's, which produces a "module defined in both" fatal at any
 * `import storm` site (project_pcm_hash_divergence). The bridge is the
 * POD-only header bench_register.h.
 *
 * Each registered benchmark is wrapped in a stable lambda: setup() runs once
 * per Range step (state.range(0) chosen by gbench), then the inner
 * `for (auto _ : state)` loop indirect-calls the trampoline run() built by
 * register.cpp.
 */

#include <benchmark/benchmark.h>
#include "bench_register.h"
#include "dashboard/reporter.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace {

    auto wire_one(storm::benchmark::RegisteredBenchmark const& reg) -> void {
        // Capture by value — the RegisteredBenchmark vector is owned by
        // register.cpp's static and outlives main(), but copying keeps the
        // closure self-contained.
        auto* bm = benchmark::RegisterBenchmark(reg.name, [reg](benchmark::State& state) {
            reg.setup(state.range(0));
            for (auto _ : state) {
                reg.run();
            }
            state.SetComplexityN(state.range(0));
            state.SetItemsProcessed(state.iterations() * state.range(0));
        });

        if (reg.sized) {
            bm->RangeMultiplier(reg.range_multiplier)
                    ->Range(reg.range_lo, reg.range_hi)
                    ->Complexity(benchmark::oN)
                    ->ArgName("N");
        } else if (!reg.args.empty()) {
            for (auto n : reg.args)
                bm->Arg(n);
            bm->Complexity(benchmark::oN)->ArgName("N");
        } else {
            bm->Arg(reg.range_lo)->ArgName("N");
        }
    }

} // namespace

namespace {

    // Find `--benchmark_filter=<value>` in argv without mutating the array —
    // gbench's Initialize re-parses it. Only the `=value` form is supported;
    // gbench's command-line parser also accepts the space-separated form,
    // but every storm_bench invocation uses `=value` and the dashboard side
    // only needs to mirror what users actually run.
    auto extract_benchmark_filter(int argc, char** argv) -> std::string {
        constexpr std::string_view prefix = "--benchmark_filter=";
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg{argv[i]};
            if (arg.starts_with(prefix))
                return std::string{arg.substr(prefix.size())};
        }
        return {};
    }

} // namespace

auto main(int argc, char** argv) -> int {
    if (!storm::benchmark::initialize_db()) {
        std::fprintf(stderr, "storm_bench: failed to open :memory: DB / create schema\n");
        return 1;
    }

    storm::benchmark::build_registration_table();
    for (auto const& reg : storm::benchmark::registered_benchmarks()) {
        wire_one(reg);
    }

    // STORM_BENCH_SOCKET defined (any value) → opt in to streaming. The path
    // is fixed by wire::default_socket_path() so both sides agree without
    // bookkeeping. Unset → fall through to gbench's default text reporter,
    // zero network calls — what CI wants.
    ::benchmark::BenchmarkReporter* dashboard_reporter = nullptr;
    if (std::getenv("STORM_BENCH_SOCKET") != nullptr) {
        const std::string filter = extract_benchmark_filter(argc, argv);
        dashboard_reporter       = bench_dashboard::install_storm_reporter(/*socket_path=*/"", filter);
    }

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv))
        return 1;
    if (dashboard_reporter != nullptr) {
        benchmark::RunSpecifiedBenchmarks(dashboard_reporter);
    } else {
        benchmark::RunSpecifiedBenchmarks();
    }
    benchmark::Shutdown();
    return 0;
}
