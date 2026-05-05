// storm_bench_dashboard — Phase 1 (Issue #247).
//
// This is a deliberate skeleton: open the DB, verify the bench_dashboard
// schema is current (BenchRun + BenchResult), report the row counts, exit.
// Phases 2+ add the AF_UNIX socket listener, the TUI, and the live-stream
// reporter wiring on the storm_bench side.
//
// Schema management is out of scope: this binary never auto-creates tables.
// Migrations are run explicitly via `cmake --build . --target migrate`
// (Atlas-backed, see docs/development/MIGRATIONS.md). If a required table is
// missing the binary prints the migration command and exits non-zero — same
// philosophy as the application schema flow.

import storm;
import <expected>;
import <memory>;
import <filesystem>;

#include "models.hpp" // textual — must follow `import storm;`

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
        const std::string default_path = default_db_path();
        std::printf(
                "storm_bench_dashboard — real-time storm_bench result dashboard\n"
                "\n"
                "Phase 1: opens the SQLite store and reports current row counts.\n"
                "\n"
                "Usage: storm_bench_dashboard [--db PATH]\n"
                "\n"
                "Options:\n"
                "  --db PATH   SQLite database path\n"
                "              (default: %s)\n"
                "  -h, --help  Show this help\n",
                default_path.c_str()
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
            "  BenchResult rows: %lld\n"
            "Phase 1 OK — schema present, exiting (socket + TUI land in Phase 2).\n",
            opts.db_path.c_str(),
            static_cast<long long>(*run_count),
            static_cast<long long>(*result_count)
    );
    return 0;
}
