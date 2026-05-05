// storm_bench_dashboard — Phase 2 (Issue #247).
//
// Phase 1 verified the SQLite schema is current. Phase 2 adds the live socket
// stream: open AF_UNIX SOCK_DGRAM, parse each NDJSON datagram from the
// storm_bench reporter, print one line per event, exit on run_complete. No
// TUI yet (Phase 3) and no DB inserts yet (Phase 3 alongside the TUI).
//
// Schema management is out of scope: this binary never auto-creates tables.
// Migrations are run explicitly via `cmake --build . --target migrate-bench`
// (Atlas-backed, see docs/development/MIGRATIONS.md). If a required table is
// missing the binary prints the migration command and exits non-zero — same
// philosophy as the application schema flow.

import storm;
import storm.bench_dashboard.socket_server;
import <expected>;
import <memory>;
import <filesystem>;

#include "models.hpp" // textual — must follow `import storm;`
#include "wire.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

    // Default location for the dashboard's SQLite store, following XDG Base
    // Directory: app-managed persistent data (history, dbs the user doesn't
    // hand-edit) belongs in $XDG_STATE_HOME, not $XDG_CONFIG_HOME. Defaults
    // to ~/.local/state/storm/dashboard/bench_results.db.
    auto default_db_path() -> std::string {
        std::filesystem::path base;
        if (const char* xdg_state = std::getenv("XDG_STATE_HOME"); xdg_state != nullptr && *xdg_state != '\0') {
            base = xdg_state;
        } else if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
            base = std::filesystem::path{home} / ".local" / "state";
        } else {
            base = ".";
        }
        return (base / "storm" / "dashboard" / "bench_results.db").string();
    }

    struct Options {
        std::string db_path{default_db_path()};
    };

    auto print_help() -> void {
        const std::string      default_db   = default_db_path();
        const std::string_view default_sock = bench_dashboard::wire::default_socket_path();
        std::printf(
                "storm_bench_dashboard — real-time storm_bench result dashboard\n"
                "\n"
                "Phase 2: streams NDJSON results from storm_bench over an AF_UNIX\n"
                "socket and prints one line per event. TUI + DB inserts land in\n"
                "Phase 3.\n"
                "\n"
                "Usage: storm_bench_dashboard [--db PATH]\n"
                "\n"
                "Options:\n"
                "  --db PATH   SQLite database path\n"
                "              (default: %s)\n"
                "  -h, --help  Show this help\n"
                "\n"
                "Listens on %.*s. To opt the bench side in to streaming, run:\n"
                "  STORM_BENCH_SOCKET=1 ./storm_bench --benchmark_filter=...\n"
                "(STORM_BENCH_SOCKET unset → bench uses the default text reporter,\n"
                " no network calls — what CI wants.)\n",
                default_db.c_str(),
                static_cast<int>(default_sock.size()),
                default_sock.data()
        );
    }

    auto parse_args(int argc, char** argv) -> Options {
        Options opts;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--db" && i + 1 < argc) {
                opts.db_path = argv[++i];
            } else if (arg == "-h" || arg == "--help") {
                print_help();
                std::exit(0);
            } else {
                std::fprintf(stderr, "storm_bench_dashboard: unknown argument: %s\n", argv[i]);
                print_help();
                std::exit(1);
            }
        }
        return opts;
    }

    // Translate a parsed wire::ResultMsg to one human-readable stdout line.
    // Phase 3 replaces this with the ANSI TUI render loop.
    auto print_msg(bench_dashboard::wire::ResultMsg const& m) -> void {
        using bench_dashboard::wire::MessageKind;
        switch (m.kind) {
        case MessageKind::RunStart:
            std::printf(
                    "[run_start]   filter=%s is_full_run=%s\n",
                    m.filter.empty() ? "<none>" : m.filter.c_str(),
                    m.is_full_run ? "true" : "false"
            );
            break;
        case MessageKind::RunComplete:
            std::printf("[run_complete]\n");
            break;
        case MessageKind::Result:
            std::printf(
                    "[result]      %-48s real=%.0fns  cpu=%.0fns  iters=%lld  ips=%.0f\n",
                    m.test_name.c_str(),
                    m.real_ns,
                    m.cpu_ns,
                    static_cast<long long>(m.iterations),
                    m.items_per_second
            );
            break;
        }
        std::fflush(stdout);
    }

    auto print_missing_schema_hint(std::string_view db_path) -> void {
        std::fprintf(
                stderr,
                "\n"
                "  The dashboard schema is missing. Bootstrap it with:\n"
                "\n"
                "      cmake --build <build-dir> --target makemigrations   # once, after model changes\n"
                "      atlas migrate apply \\\n"
                "          --dir 'file://benchmarks/dashboard/migrations' \\\n"
                "          --url 'sqlite://%.*s'\n",
                static_cast<int>(db_path.size()),
                db_path.data()
        );
    }

} // namespace

auto main(int argc, char** argv) -> int {
    const auto opts = parse_args(argc, argv);

    // Default location is under $XDG_STATE_HOME — the parent dir won't exist
    // on first run. Create it so SQLite's open call doesn't fail with ENOENT.
    if (const auto parent = std::filesystem::path{opts.db_path}.parent_path(); !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            std::fprintf(
                    stderr, "storm_bench_dashboard: cannot create '%s': %s\n", parent.c_str(), ec.message().c_str()
            );
            return 1;
        }
    }

    if (auto rc = storm::QuerySet<bench_dashboard::BenchRun>::set_default_connection(opts.db_path); !rc) {
        std::fprintf(
                stderr,
                "storm_bench_dashboard: cannot open '%s': %s\n",
                opts.db_path.c_str(),
                std::string{rc.error().message()}.c_str()
        );
        return 1;
    }

    auto run_count    = storm::QuerySet<bench_dashboard::BenchRun>().count().execute();
    auto result_count = storm::QuerySet<bench_dashboard::BenchResult>().count().execute();

    if (!run_count || !result_count) {
        const std::string err_msg{!run_count ? run_count.error().message() : result_count.error().message()};
        std::fprintf(stderr, "storm_bench_dashboard: schema check failed: %s\n", err_msg.c_str());
        print_missing_schema_hint(opts.db_path);
        return 2;
    }

    std::printf(
            "storm_bench_dashboard: opened %s\n"
            "  BenchRun rows:    %lld\n"
            "  BenchResult rows: %lld\n",
            opts.db_path.c_str(),
            static_cast<long long>(*run_count),
            static_cast<long long>(*result_count)
    );

    const std::string_view socket_path = bench_dashboard::wire::default_socket_path();

    bench_dashboard::SocketServer server;
    if (auto err = server.open(socket_path); !err.empty()) {
        std::fprintf(stderr, "storm_bench_dashboard: %s\n", err.c_str());
        return 3;
    }
    std::printf(
            "Listening on %.*s — run storm_bench with STORM_BENCH_SOCKET=1\n"
            "Press Ctrl-C to stop.\n",
            static_cast<int>(socket_path.size()),
            socket_path.data()
    );
    std::fflush(stdout);

    // Block forever; one storm_bench session = one run_start ... run_complete
    // window. After run_complete we keep listening so the same dashboard
    // process can serve multiple bench invocations (Phase 3 makes this a
    // multi-session stacked view; Phase 2 just prints them sequentially).
    for (;;) {
        auto datagram = server.recv_one(/*timeout_ms=*/-1);
        if (!datagram)
            continue;
        auto msg = bench_dashboard::wire::parse(*datagram);
        if (!msg) {
            std::fprintf(stderr, "storm_bench_dashboard: skipped unparseable datagram (%zu bytes)\n", datagram->size());
            continue;
        }
        print_msg(*msg);
    }
}
