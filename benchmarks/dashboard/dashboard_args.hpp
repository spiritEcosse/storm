#pragma once

// CLI parsing for storm_bench_dashboard (Issue #247).
//
// Textual header — `#include`d from the anonymous namespace of main.cpp so
// every helper here lives at internal linkage in that TU. Split out of
// main.cpp purely to keep each TU under the 600-line code-quality cap.
// Do NOT include this from any other TU.

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

namespace {

    // -----------------------------------------------------------------------
    // CLI defaults
    // -----------------------------------------------------------------------

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

    // Default backup repo + tag for `--upload-backup` / `--restore-backup`.
    // The repo must exist (and be private) — gh CLI does not create it.
    inline constexpr std::string_view kDefaultBackupRepo = "spiritEcosse/storm-bench-private";

    auto default_backup_tag() -> std::string {
        // Hostname is used so multiple machines can share one backup repo
        // without colliding. `hostname -s` matches what insert_run() stores.
        std::string host;
        if (std::unique_ptr<std::FILE, decltype(&pclose)> pipe{::popen("hostname -s 2>/dev/null", "r"), &::pclose};
            pipe) {
            char buf[128];
            if (std::fgets(buf, sizeof(buf), pipe.get()) != nullptr)
                host.assign(buf);
            while (!host.empty() && (host.back() == '\n' || host.back() == '\r'))
                host.pop_back();
        }
        if (host.empty())
            host = "unknown";
        return std::format("bench-backup-{}", host);
    }

    // Phase 6: baseline selector variants.
    struct BaselineAuto {};
    struct BaselineNone {};
    struct BaselineRunId {
        std::int64_t id{};
    };
    struct BaselineBranch {
        std::string name{};
    };
    using BaselineSelector = std::variant<BaselineAuto, BaselineNone, BaselineRunId, BaselineBranch>;

    struct Options {
        std::string      db_path{default_db_path()};
        std::string      socket_path{};   // empty → wire::default_socket_path()
        bool             order_arrival{}; // false → newest-first (default)
        bool             upload_backup{};
        bool             restore_backup{};
        std::string      backup_repo{kDefaultBackupRepo};
        std::string      backup_tag{}; // empty → default_backup_tag()
        BaselineSelector baseline{BaselineAuto{}};
        double           regression_threshold{5.0};
        double           complexity_threshold{5.0}; // Phase 7: coefficient drift threshold
    };

    auto print_help() -> void {
        const std::string      default_db   = default_db_path();
        const std::string_view default_sock = bench_dashboard::wire::default_socket_path();
        const std::string      default_tag  = default_backup_tag();
        std::printf(
                "storm_bench_dashboard — real-time storm_bench result dashboard\n"
                "\n"
                "Usage: storm_bench_dashboard [options]\n"
                "\n"
                "Options:\n"
                "  --db PATH               SQLite database path\n"
                "                          (default: %s)\n"
                "  --socket PATH           AF_UNIX socket path to listen on\n"
                "                          (default: %.*s)\n"
                "  --order arrival         Render results in arrival order inside each\n"
                "                          category (default: newest-first)\n"
                "  --baseline SELECTOR     Baseline for regression comparison:\n"
                "                            auto        most recent full run, same branch+host (default)\n"
                "                            none        disable comparison\n"
                "                            run:<id>    specific run by numeric id\n"
                "                            branch:<name>  most recent full run on named branch\n"
                "  --regression-threshold N\n"
                "                          Percentage delta that counts as a regression\n"
                "                          (default: 5)\n"
                "  --complexity-threshold N\n"
                "                          Coefficient drift percentage that counts as a\n"
                "                          complexity regression (default: 5)\n"
                "  --upload-backup         One-shot: upload --db PATH to the backup repo\n"
                "                          via `gh release upload --clobber` and exit\n"
                "  --restore-backup        One-shot: download the DB file from the backup\n"
                "                          repo via `gh release download` and exit\n"
                "  --backup-repo OWNER/REPO  Backup repo (default: %.*s)\n"
                "  --backup-tag NAME       Release tag (default: %s)\n"
                "  -h, --help              Show this help\n"
                "\n"
                "Listens on %.*s. To opt the bench side in to streaming, run:\n"
                "  STORM_BENCH_SOCKET=1 ./storm_bench --benchmark_filter=...\n"
                "(STORM_BENCH_SOCKET unset → bench uses the default text reporter,\n"
                " no network calls — what CI wants.)\n"
                "\n"
                "--upload-backup / --restore-backup require `gh auth login` to be set up\n"
                "in advance. The dashboard never auto-uploads on quit.\n",
                default_db.c_str(),
                static_cast<int>(default_sock.size()),
                default_sock.data(),
                static_cast<int>(kDefaultBackupRepo.size()),
                kDefaultBackupRepo.data(),
                default_tag.c_str(),
                static_cast<int>(default_sock.size()),
                default_sock.data()
        );
    }

    // -----------------------------------------------------------------------
    // Per-flag parsers (see parse_args)
    // -----------------------------------------------------------------------

    // Parse the value side of `--order arrival|newest`. Exits with usage on
    // any other token so the caller can stay flat.
    auto parse_order_arg(std::string_view mode, Options& opts) -> void {
        if (mode == "arrival") {
            opts.order_arrival = true;
            return;
        }
        if (mode == "newest") {
            opts.order_arrival = false;
            return;
        }
        std::fprintf(
                stderr,
                "storm_bench_dashboard: --order expects 'arrival' or 'newest', got: %.*s\n",
                static_cast<int>(mode.size()),
                mode.data()
        );
        std::exit(1);
    }

    // Parse the value side of `--baseline auto|none|run:<id>|branch:<name>`.
    auto parse_baseline_arg(std::string_view sel, Options& opts) -> void {
        if (sel == "auto") {
            opts.baseline = BaselineAuto{};
            return;
        }
        if (sel == "none") {
            opts.baseline = BaselineNone{};
            return;
        }
        if (sel.starts_with("run:")) {
            std::int64_t id{};
            const auto   sv = sel.substr(4);
            const auto   r  = std::from_chars(sv.data(), sv.data() + sv.size(), id);
            if (r.ec != std::errc{} || id <= 0) {
                std::fprintf(stderr, "storm_bench_dashboard: --baseline run: expects a positive integer id\n");
                std::exit(1);
            }
            opts.baseline = BaselineRunId{id};
            return;
        }
        if (sel.starts_with("branch:")) {
            opts.baseline = BaselineBranch{std::string{sel.substr(7)}};
            return;
        }
        std::fprintf(
                stderr,
                "storm_bench_dashboard: --baseline expects auto|none|run:<id>|branch:<name>, got: %.*s\n",
                static_cast<int>(sel.size()),
                sel.data()
        );
        std::exit(1);
    }

    // Shared parsing for `--regression-threshold` and `--complexity-threshold`.
    // `name` is the user-facing flag (printed in the error). Exits on bad input.
    auto parse_threshold_arg(std::string_view name, std::string_view sv, double& out) -> void {
        double     v{};
        const auto r = std::from_chars(sv.data(), sv.data() + sv.size(), v);
        if (r.ec != std::errc{} || v <= 0.0) {
            std::fprintf(
                    stderr,
                    "storm_bench_dashboard: %.*s expects a positive number, got: %.*s\n",
                    static_cast<int>(name.size()),
                    name.data(),
                    static_cast<int>(sv.size()),
                    sv.data()
            );
            std::exit(1);
        }
        out = v;
    }

    // Try the boolean flags (no value). Returns true if `arg` matched.
    auto try_apply_bool_flag(std::string_view arg, Options& opts) -> bool {
        if (arg == "--upload-backup") {
            opts.upload_backup = true;
            return true;
        }
        if (arg == "--restore-backup") {
            opts.restore_backup = true;
            return true;
        }
        if (arg == "-h" || arg == "--help") {
            print_help();
            std::exit(0);
        }
        return false;
    }

    // Try the string-valued flags (consume the next argv slot via `i`).
    // Returns true if `arg` matched. Caller is responsible for the i+1<argc
    // bound check before delegating.
    auto try_apply_string_flag(std::string_view arg, Options& opts, char const* value) -> bool {
        if (arg == "--db") {
            opts.db_path = value;
            return true;
        }
        if (arg == "--socket") {
            opts.socket_path = value;
            return true;
        }
        if (arg == "--backup-repo") {
            opts.backup_repo = value;
            return true;
        }
        if (arg == "--backup-tag") {
            opts.backup_tag = value;
            return true;
        }
        return false;
    }

    // Try the structured-value flags (need their own parser).
    auto try_apply_structured_flag(std::string_view arg, Options& opts, char const* value) -> bool {
        if (arg == "--order") {
            parse_order_arg(value, opts);
            return true;
        }
        if (arg == "--baseline") {
            parse_baseline_arg(value, opts);
            return true;
        }
        if (arg == "--regression-threshold") {
            parse_threshold_arg("--regression-threshold", value, opts.regression_threshold);
            return true;
        }
        if (arg == "--complexity-threshold") {
            parse_threshold_arg("--complexity-threshold", value, opts.complexity_threshold);
            return true;
        }
        return false;
    }

    // Apply one CLI flag (and its value, if it takes one). `i` advances when
    // the flag consumes the next argv slot. Returns false on unknown flag so
    // the caller can decide how to abort.
    auto apply_one_arg(Options& opts, int argc, char** argv, int& i) -> bool {
        const std::string_view arg = argv[i];
        if (try_apply_bool_flag(arg, opts))
            return true;
        if (i + 1 >= argc)
            return false;
        char const* value = argv[i + 1];
        if (try_apply_string_flag(arg, opts, value) || try_apply_structured_flag(arg, opts, value)) {
            ++i;
            return true;
        }
        return false;
    }

    auto parse_args(int argc, char** argv) -> Options {
        Options opts;
        for (int i = 1; i < argc; ++i) {
            if (!apply_one_arg(opts, argc, argv, i)) {
                std::fprintf(stderr, "storm_bench_dashboard: unknown argument: %s\n", argv[i]);
                print_help();
                std::exit(1);
            }
        }
        if (opts.upload_backup && opts.restore_backup) {
            std::fprintf(
                    stderr, "storm_bench_dashboard: --upload-backup and --restore-backup are mutually exclusive\n"
            );
            std::exit(1);
        }
        if (opts.backup_tag.empty())
            opts.backup_tag = default_backup_tag();
        return opts;
    }

} // namespace
