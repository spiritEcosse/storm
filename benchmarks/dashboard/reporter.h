#pragma once

// install_storm_reporter() — wires a custom Google Benchmark reporter that
// streams every Run as a one-line NDJSON datagram to the storm_bench_dashboard
// (Issue #247, Phase 2).
//
// Plain textual header. Both this header and the .cpp it pairs with stay
// outside any module purview because they include <benchmark/benchmark.h> —
// putting that header in a .cppm purview crashes clang-p2996's BMI writer
// (memory feedback_benchmark_macro_in_module_purview), and mixing it with
// `import storm` in one TU diverges PCM-cache hashes vs libstorm.a (memory
// project_pcm_hash_divergence). The reporter never imports storm.

#include <string_view>

namespace benchmark {
class BenchmarkReporter;
}

namespace bench_dashboard {

// Install a custom reporter that emits NDJSON over an AF_UNIX SOCK_DGRAM
// socket. If the socket is not reachable (dashboard offline) the function
// prints a single warning to stderr and bench continues with the default
// gbench reporter — never aborts. `socket_path` empty → use the default
// path returned by wire::default_socket_path(). `filter` is the value of
// --benchmark_filter (empty = full run); the reporter forwards it to the
// dashboard in the run_start sentinel so the dashboard can tag partial
// runs.
//
// Must be called BEFORE benchmark::Initialize() (or at least before
// RunSpecifiedBenchmarks). Returns the reporter to pass to
// RunSpecifiedBenchmarks(reporter), or nullptr on graceful degradation —
// in which case main() should call the no-arg RunSpecifiedBenchmarks().
// The returned pointer is owned by the implementation (process lifetime,
// closes the socket and emits run_complete on its destructor).
auto install_storm_reporter(std::string_view socket_path, std::string_view filter) -> ::benchmark::BenchmarkReporter *;

} // namespace bench_dashboard
