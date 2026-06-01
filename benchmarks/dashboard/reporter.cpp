// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg,concurrency-mt-unsafe,cppcoreguidelines-special-member-functions)
// Streaming Google Benchmark reporter that pipes results to the
// storm_bench_dashboard over an AF_UNIX SOCK_DGRAM socket (Issue #247,
// Phase 2).
//
// This TU is plain .cpp by design:
//
//   - Includes <benchmark/benchmark.h>. Putting that include inside a .cppm
//     purview crashes clang-p2996's BMI writer (memory
//     feedback_benchmark_macro_in_module_purview).
//   - Never `import storm`. Mixing gbench + import storm in one TU diverges
//     this TU's PCM-cache hash from libstorm.a's, producing a
//     "module defined in both" fatal at any import storm site (memory
//     project_pcm_hash_divergence, feedback_bench_main_register_split).
//
// Wire format and graceful-degradation behaviour are documented in
// docs/development/BENCHMARK_DASHBOARD.md once the docs land in Phase 5.

#include "reporter.h"
#include "row_classify.hpp"
#include "wire.hpp"

#include <benchmark/benchmark.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

namespace bench_dashboard {

    namespace {

        // The first segment after "Storm/" is the category in this project's
        // benchmark naming convention (e.g. "Storm/WHERE/where_int_gt/1024" →
        // "WHERE"). Names that don't follow the convention fall back to "?".
        auto big_o_string(benchmark::BigO c) -> std::string_view {
            switch (c) {
            case benchmark::oN:
                return "N";
            case benchmark::oNSquared:
                return "N^2";
            case benchmark::oNCubed:
                return "N^3";
            case benchmark::oLogN:
                return "lgN";
            case benchmark::oNLogN:
                return "NlgN";
            case benchmark::o1:
                return "(1)";
            default:
                return "f(N)";
            }
        }

        auto extract_category(std::string_view name) -> std::string {
            constexpr std::string_view prefix = "Storm/";
            if (!name.starts_with(prefix)) {
                return "?";
            }
            const auto rest = name.substr(prefix.size());
            const auto end  = rest.find('/');
            return std::string{rest.substr(0, end == std::string_view::npos ? rest.size() : end)};
        }

        // Initial msg shell with kind+name+category populated. Each ReportRuns
        // branch then fills in its row_kind-specific fields.
        auto base_msg_for_run(benchmark::BenchmarkReporter::Run const& r) -> wire::ResultMsg {
            wire::ResultMsg m{};
            m.kind      = wire::MessageKind::Result;
            m.test_name = r.benchmark_name();
            m.category  = extract_category(m.test_name);
            return m;
        }

        auto build_bigo_msg(benchmark::BenchmarkReporter::Run const& r) -> wire::ResultMsg {
            auto m             = base_msg_for_run(r);
            m.row_kind         = std::string{wire::kRowKindBigO};
            m.complexity_class = std::string{big_o_string(r.complexity)};
            // In a BigO row, real_accumulated_time holds the coefficient.
            m.complexity_coef = r.real_accumulated_time;
            return m;
        }

        auto build_rms_msg(benchmark::BenchmarkReporter::Run const& r) -> wire::ResultMsg {
            auto m     = base_msg_for_run(r);
            m.row_kind = std::string{wire::kRowKindRms};
            // In an RMS row, real_accumulated_time holds the RMS percentage.
            m.rms_pct = r.real_accumulated_time * 100.0;
            return m;
        }

        // Adapt a gbench Run into the gbench-free RowFlags used by the pure
        // classifier in row_classify.hpp.
        auto flags_of(benchmark::BenchmarkReporter::Run const& r) -> RowFlags {
            return RowFlags{
                    .skipped        = r.skipped != benchmark::internal::NotSkipped,
                    .aggregate_name = r.aggregate_name,
                    .report_big_o   = r.report_big_o,
                    .report_rms     = r.report_rms,
            };
        }

        // Raw per-repetition rows classify as "measurement"; mean/median/stddev
        // summary rows (the only rows emitted under
        // --benchmark_report_aggregates_only=true) classify as "aggregate". Both
        // carry real timing data and share the same message shape (Issue #265).
        auto build_measurement_msg(benchmark::BenchmarkReporter::Run const& r) -> wire::ResultMsg {
            auto m         = base_msg_for_run(r);
            m.row_kind     = std::string{classify_row_kind(flags_of(r))};
            m.dataset_size = static_cast<std::int64_t>(r.complexity_n);
            m.iterations   = static_cast<std::int64_t>(r.iterations);

            // real_accumulated_time / cpu_accumulated_time are in seconds
            // (gbench source: src/reporter.cc:117). Convert to ns/iter by
            // × 1e9 / iterations. Guard iterations=0 (matches gbench's
            // GetAdjustedRealTime).
            const double iters_safe = r.iterations > 0 ? static_cast<double>(r.iterations) : 1.0;
            m.real_ns               = r.real_accumulated_time * 1.0e9 / iters_safe;
            m.cpu_ns                = r.cpu_accumulated_time * 1.0e9 / iters_safe;

            if (auto it = r.counters.find("items_per_second"); it != r.counters.end()) {
                m.items_per_second = it->second.value;
            } else if (m.real_ns > 0.0 && m.dataset_size > 0) {
                m.items_per_second = static_cast<double>(m.dataset_size) / (m.real_ns * 1.0e-9);
            }
            return m;
        }

        // Pre-filter: discard only genuinely-skipped runs. Aggregate rows
        // (mean/median/stddev) flow through — dropping them silently discarded
        // every timing under --benchmark_report_aggregates_only=true (Issue
        // #265). Classification into measurement/aggregate/bigo/rms is delegated
        // to row_classify.hpp.
        auto should_skip_run(benchmark::BenchmarkReporter::Run const& r) -> bool {
            return should_skip_row(flags_of(r));
        }

        class StormReporter
                final // NOLINT(cppcoreguidelines-special-member-functions) — destructor only; move/copy intentionally not needed
            : public benchmark::BenchmarkReporter {
          public:
            StormReporter(int file_fd, std::string filter) : fd_{file_fd}, filter_{std::move(filter)} {}

            ~StormReporter() override {
                send_line(wire::build_run_complete());
                if (fd_ >= 0) {
                    ::close(fd_);
                }
            }

            auto ReportContext(Context const& /*ctx*/) -> bool override {
                if (!sent_start_) {
                    send_line(wire::build_run_start(filter_, filter_.empty(), /*is_raw=*/false));
                    sent_start_ = true;
                }
                return true;
            }

            auto ReportRuns(std::vector<Run> const& runs) -> void override {
                for (auto const& r : runs) {
                    if (should_skip_run(r)) {
                        continue;
                    }
                    if (r.report_big_o) {
                        send_line(wire::build_result(build_bigo_msg(r)));
                    } else if (r.report_rms) {
                        send_line(wire::build_result(build_rms_msg(r)));
                    } else {
                        send_line(wire::build_result(build_measurement_msg(r)));
                    }
                }
            }

          private:
            auto send_line(std::string const& line) -> void {
                if (fd_ < 0) {
                    return;
                }
                // MSG_DONTWAIT: never block the bench loop on a slow consumer.
                // MSG_NOSIGNAL: don't take SIGPIPE if the dashboard exits.
                const ssize_t n = ::send(fd_, line.data(), line.size(), MSG_DONTWAIT | MSG_NOSIGNAL);
                if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    // Lose the connection — close fd, every subsequent send
                    // becomes a no-op. We don't flap stderr on every result.
                    ::close(fd_);
                    fd_ = -1;
                }
            }

            int         fd_{-1};
            std::string filter_; // NOLINT(readability-redundant-member-init) — explicit default is intentional style
            bool        sent_start_{false};
        };

    } // namespace

    auto install_storm_reporter(std::string_view socket_path, std::string_view filter)
            -> ::benchmark::BenchmarkReporter* {
        const std::string path =
                socket_path.empty() ? std::string{wire::default_socket_path()} : std::string{socket_path};
        if (path.size() >= sizeof(sockaddr_un{}.sun_path)) {
            std::
                    fprintf( // NOLINT(cppcoreguidelines-pro-type-vararg)
                            stderr,
                            "storm_bench: dashboard socket path too long, falling back to text reporter\n"
                    );
            return nullptr;
        }

        const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            std::
                    fprintf( // NOLINT(cppcoreguidelines-pro-type-vararg)
                            stderr,
                            "storm_bench: socket(): %s — falling back to text reporter\n",
                            std::strerror(errno) // NOLINT(concurrency-mt-unsafe)
                    );
            return nullptr;
        }

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, path.data(), path.size());
        addr.sun_path[path.size()] = '\0';

        // Portable addrlen — only the bytes actually used in sun_path plus
        // the trailing NUL. Using sizeof(addr) over-counts and trips lints.
        const auto addr_len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
        if (::connect(fd, reinterpret_cast<sockaddr const*>(&addr), addr_len) != 0) {
            std::
                    fprintf( // NOLINT(cppcoreguidelines-pro-type-vararg,concurrency-mt-unsafe)
                            stderr,
                            "storm_bench: dashboard not reachable at %s (%s) — running with default reporter\n",
                            path.c_str(),
                            std::strerror(errno)
                    );
            ::close(fd);
            return nullptr;
        }

        // Process-lifetime singleton — RunSpecifiedBenchmarks does not take
        // ownership, and the destructor (which emits run_complete + closes
        // the fd) needs to fire at process exit. Held by static unique_ptr
        // for clean teardown order.
        static std::unique_ptr<StormReporter> g_reporter;
        g_reporter = std::make_unique<StormReporter>(fd, std::string{filter});
        return g_reporter.get();
    }

} // namespace bench_dashboard
// NOLINTEND(cppcoreguidelines-pro-type-vararg,concurrency-mt-unsafe,cppcoreguidelines-special-member-functions)
