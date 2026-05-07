// storm_bench_dashboard — Phase 3 (Issue #247).
//
// Phase 1 verified the SQLite schema is current. Phase 2 added the live socket
// stream. Phase 3 turns the binary into a real TUI: every NDJSON datagram is
// parsed, persisted to SQLite via Storm (`QuerySet<BenchRun>` /
// `QuerySet<BenchResult>` — Storm dogfoods itself), and rendered in an
// alt-screen ANSI view with categorised, colour-coded, newest-first results.
// Multiple `storm_bench` invocations stack in the same dashboard process
// (newest on top); older sessions auto-collapse and re-expand on number keys.
//
// Schema management is out of scope: this binary never auto-creates tables.
// Migrations are run explicitly via `cmake --build . --target migrate-bench`
// (Atlas-backed, see docs/development/MIGRATIONS.md). If a required table is
// missing the binary prints the migration command and exits non-zero — same
// philosophy as the application schema flow.

import storm;
import storm.bench_dashboard.socket_server;
import storm.bench_dashboard.tui;
import <expected>;
import <filesystem>;
import <memory>;
import <ranges>;

#include "models.hpp" // textual — must follow `import storm;`
#include "wire.hpp"

#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace {

    // SIGINT / SIGTERM → flip the flag the main loop already checks for `q`.
    // Keeps the alt-screen + termios teardown on the normal exit path so a
    // Ctrl-C doesn't leave the user with a wrecked terminal.
    std::atomic<bool> g_should_quit{false};

    extern "C" auto handle_quit_signal(int /*sig*/) -> void {
        g_should_quit.store(true);
    }

    auto install_signal_handlers() -> void {
        struct sigaction sa{};
        sa.sa_handler = &handle_quit_signal;
        ::sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0; // intentionally not SA_RESTART — we want poll() to wake on signal
        ::sigaction(SIGINT, &sa, nullptr);
        ::sigaction(SIGTERM, &sa, nullptr);
    }

    // -----------------------------------------------------------------------
    // CLI parsing
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

    auto parse_args(int argc, char** argv) -> Options {
        Options opts;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--db" && i + 1 < argc) {
                opts.db_path = argv[++i];
            } else if (arg == "--socket" && i + 1 < argc) {
                opts.socket_path = argv[++i];
            } else if (arg == "--order" && i + 1 < argc) {
                const std::string_view mode = argv[++i];
                if (mode == "arrival") {
                    opts.order_arrival = true;
                } else if (mode == "newest") {
                    opts.order_arrival = false;
                } else {
                    std::fprintf(
                            stderr,
                            "storm_bench_dashboard: --order expects 'arrival' or 'newest', got: %.*s\n",
                            static_cast<int>(mode.size()),
                            mode.data()
                    );
                    std::exit(1);
                }
            } else if (arg == "--baseline" && i + 1 < argc) {
                const std::string_view sel = argv[++i];
                if (sel == "auto") {
                    opts.baseline = BaselineAuto{};
                } else if (sel == "none") {
                    opts.baseline = BaselineNone{};
                } else if (sel.starts_with("run:")) {
                    std::int64_t id{};
                    const auto   sv = sel.substr(4);
                    const auto   r  = std::from_chars(sv.data(), sv.data() + sv.size(), id);
                    if (r.ec != std::errc{} || id <= 0) {
                        std::fprintf(stderr, "storm_bench_dashboard: --baseline run: expects a positive integer id\n");
                        std::exit(1);
                    }
                    opts.baseline = BaselineRunId{id};
                } else if (sel.starts_with("branch:")) {
                    opts.baseline = BaselineBranch{std::string{sel.substr(7)}};
                } else {
                    std::fprintf(
                            stderr,
                            "storm_bench_dashboard: --baseline expects auto|none|run:<id>|branch:<name>, got: %.*s\n",
                            static_cast<int>(sel.size()),
                            sel.data()
                    );
                    std::exit(1);
                }
            } else if (arg == "--regression-threshold" && i + 1 < argc) {
                const std::string_view sv = argv[++i];
                double                 v{};
                const auto             r = std::from_chars(sv.data(), sv.data() + sv.size(), v);
                if (r.ec != std::errc{} || v <= 0.0) {
                    std::fprintf(
                            stderr,
                            "storm_bench_dashboard: --regression-threshold expects a positive number, got: %.*s\n",
                            static_cast<int>(sv.size()),
                            sv.data()
                    );
                    std::exit(1);
                }
                opts.regression_threshold = v;
            } else if (arg == "--complexity-threshold" && i + 1 < argc) {
                const std::string_view sv = argv[++i];
                double                 v{};
                const auto             r = std::from_chars(sv.data(), sv.data() + sv.size(), v);
                if (r.ec != std::errc{} || v <= 0.0) {
                    std::fprintf(
                            stderr,
                            "storm_bench_dashboard: --complexity-threshold expects a positive number, got: %.*s\n",
                            static_cast<int>(sv.size()),
                            sv.data()
                    );
                    std::exit(1);
                }
                opts.complexity_threshold = v;
            } else if (arg == "--upload-backup") {
                opts.upload_backup = true;
            } else if (arg == "--restore-backup") {
                opts.restore_backup = true;
            } else if (arg == "--backup-repo" && i + 1 < argc) {
                opts.backup_repo = argv[++i];
            } else if (arg == "--backup-tag" && i + 1 < argc) {
                opts.backup_tag = argv[++i];
            } else if (arg == "-h" || arg == "--help") {
                print_help();
                std::exit(0);
            } else {
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

    // -----------------------------------------------------------------------
    // Backup / restore helpers
    //
    // Both operations shell out to `gh` because that's the path of least
    // resistance: gh handles auth, retries, and rate limits. Pre-existing
    // `gh auth login` is required — we surface a clear error otherwise.
    // -----------------------------------------------------------------------

    // Wrap a string for safe inclusion as a single-quoted shell argument.
    // GNU shells allow any byte except `'` inside single quotes, so an
    // embedded apostrophe must close the quote, append an escaped `\'`, and
    // reopen the quote. Used for db paths, repo, and tag values that flow
    // unmodified into a popen() command line.
    auto shell_single_quote(std::string_view s) -> std::string {
        std::string out;
        out.reserve(s.size() + 2);
        out += '\'';
        for (char c : s) {
            if (c == '\'') {
                out += "'\\''";
            } else {
                out += c;
            }
        }
        out += '\'';
        return out;
    }

    // Run `cmd`, capture combined stdout+stderr, return exit_status + output.
    // popen()'s default mode "r" forwards stderr through the parent shell
    // unless the caller redirects — we pass `2>&1` in the command string so
    // gh's friendly error messages reach us here.
    struct ShellResult {
        int         exit_status{};
        std::string output{};
    };

    auto run_shell(std::string const& cmd) -> ShellResult {
        ShellResult                                   result;
        std::unique_ptr<std::FILE, decltype(&pclose)> pipe{::popen(cmd.c_str(), "r"), &::pclose};
        if (!pipe) {
            result.exit_status = -1;
            result.output      = std::format("popen() failed: {}", std::strerror(errno));
            return result;
        }
        char buf[1024];
        while (std::fgets(buf, sizeof(buf), pipe.get()) != nullptr)
            result.output.append(buf);
        // Reclaim ownership so we can read pclose()'s return code; the
        // unique_ptr destructor would otherwise discard it.
        std::FILE* raw     = pipe.release();
        const int  rc      = ::pclose(raw);
        result.exit_status = rc;
        return result;
    }

    auto check_gh_auth() -> std::expected<void, std::string> {
        const auto rc = run_shell("gh auth status >/dev/null 2>&1");
        if (rc.exit_status != 0)
            return std::unexpected(std::string{"gh CLI is not authenticated — run `gh auth login` first"});
        return {};
    }

    // Verify the backup repo exists and is reachable. Returns the gh stderr
    // on failure so the caller can surface the actual reason (missing,
    // private without access, etc.).
    auto check_backup_repo_exists(std::string_view repo) -> std::expected<void, std::string> {
        const auto cmd = std::format("gh repo view {} >/dev/null 2>&1", shell_single_quote(repo));
        const auto rc  = run_shell(cmd);
        if (rc.exit_status != 0)
            return std::unexpected(
                    std::format(
                            "backup repo '{}' is not reachable — create it first: gh repo create {} --private",
                            repo,
                            repo
                    )
            );
        return {};
    }

    // One-shot upload. Uses `gh release upload --clobber` so re-running
    // overwrites the existing asset for that tag. Tag is created on first
    // upload via `gh release create`; subsequent uploads attach to it.
    auto upload_backup(Options const& opts) -> int {
        if (auto rc = check_gh_auth(); !rc) {
            std::fprintf(stderr, "storm_bench_dashboard: %s\n", rc.error().c_str());
            return 1;
        }
        if (auto rc = check_backup_repo_exists(opts.backup_repo); !rc) {
            std::fprintf(stderr, "storm_bench_dashboard: %s\n", rc.error().c_str());
            return 1;
        }
        if (std::error_code ec; !std::filesystem::exists(opts.db_path, ec) || ec) {
            std::fprintf(stderr, "storm_bench_dashboard: db file not found: %s\n", opts.db_path.c_str());
            return 1;
        }

        const auto repo_q = shell_single_quote(opts.backup_repo);
        const auto tag_q  = shell_single_quote(opts.backup_tag);
        const auto file_q = shell_single_quote(opts.db_path);

        // gh release upload fails if the release does not yet exist for the
        // tag. Try `release view` first; create the release on miss.
        const auto view_cmd = std::format("gh release view {} --repo {} >/dev/null 2>&1", tag_q, repo_q);
        if (run_shell(view_cmd).exit_status != 0) {
            const auto create_cmd = std::format(
                    "gh release create {} --repo {} --notes 'storm_bench dashboard backup' 2>&1", tag_q, repo_q
            );
            const auto create_rc = run_shell(create_cmd);
            if (create_rc.exit_status != 0) {
                std::fprintf(stderr, "storm_bench_dashboard: gh release create failed: %s\n", create_rc.output.c_str());
                return 1;
            }
        }

        const auto upload_cmd = std::format("gh release upload --clobber {} {} --repo {} 2>&1", tag_q, file_q, repo_q);
        const auto upload_rc  = run_shell(upload_cmd);
        if (upload_rc.exit_status != 0) {
            std::fprintf(stderr, "storm_bench_dashboard: gh release upload failed: %s\n", upload_rc.output.c_str());
            return 1;
        }
        std::fprintf(
                stdout,
                "storm_bench_dashboard: uploaded %s to %s@%s\n",
                opts.db_path.c_str(),
                opts.backup_repo.c_str(),
                opts.backup_tag.c_str()
        );
        return 0;
    }

    // One-shot restore. Downloads the asset matching the db filename into
    // the parent directory of --db. We deliberately do not auto-rename the
    // existing local file; the user is warned to move it themselves.
    auto restore_backup(Options const& opts) -> int {
        if (auto rc = check_gh_auth(); !rc) {
            std::fprintf(stderr, "storm_bench_dashboard: %s\n", rc.error().c_str());
            return 1;
        }
        if (auto rc = check_backup_repo_exists(opts.backup_repo); !rc) {
            std::fprintf(stderr, "storm_bench_dashboard: %s\n", rc.error().c_str());
            return 1;
        }

        const std::filesystem::path db{opts.db_path};
        const std::filesystem::path dir      = db.parent_path().empty() ? std::filesystem::path{"."} : db.parent_path();
        const std::string           filename = db.filename().string();

        if (std::error_code ec; std::filesystem::exists(opts.db_path, ec) && !ec) {
            std::fprintf(
                    stderr,
                    "storm_bench_dashboard: warning — '%s' already exists; "
                    "rename or move it first if you want to keep the current copy. "
                    "Restore will overwrite it.\n",
                    opts.db_path.c_str()
            );
        }

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            std::fprintf(stderr, "storm_bench_dashboard: cannot create '%s': %s\n", dir.c_str(), ec.message().c_str());
            return 1;
        }

        const auto repo_q    = shell_single_quote(opts.backup_repo);
        const auto tag_q     = shell_single_quote(opts.backup_tag);
        const auto dir_q     = shell_single_quote(dir.string());
        const auto pattern_q = shell_single_quote(filename);
        const auto cmd       = std::format(
                "gh release download {} --repo {} --dir {} --pattern {} --clobber 2>&1", tag_q, repo_q, dir_q, pattern_q
        );
        const auto rc = run_shell(cmd);
        if (rc.exit_status != 0) {
            std::fprintf(stderr, "storm_bench_dashboard: gh release download failed: %s\n", rc.output.c_str());
            return 1;
        }
        std::fprintf(
                stdout,
                "storm_bench_dashboard: restored %s from %s@%s\n",
                opts.db_path.c_str(),
                opts.backup_repo.c_str(),
                opts.backup_tag.c_str()
        );
        return 0;
    }

    // -----------------------------------------------------------------------
    // Phase 6: baseline resolution + per-result delta lookup
    // -----------------------------------------------------------------------

    // Resolve the baseline selector to a concrete (run_id, label) pair.
    // Returns {0, ""} when no matching run is found or selector is None.
    // Must be called AFTER set_default_connection() so Storm queries work.
    auto resolve_baseline(BaselineSelector const& sel, std::string_view current_branch, std::string_view current_host)
            -> std::pair<std::int64_t, std::string> {
        if (std::holds_alternative<BaselineNone>(sel))
            return {0, ""};

        auto find_run = [&](auto qs) -> std::pair<std::int64_t, std::string> {
            auto rows = qs.template order_by<^^bench_dashboard::BenchRun::id, false>()
                                .limit(1)
                                .select()
                                .execute()
                                .transform([](auto&& hive) {
                                    return std::forward<decltype(hive)>(hive) | std::views::as_rvalue |
                                           std::ranges::to<std::vector<bench_dashboard::BenchRun>>();
                                });
            if (!rows || rows->empty())
                return {0, ""};
            const auto& r   = rows->front();
            const auto  lbl = std::format("Run #{} {} {}", r.id, r.branch, r.git_hash);
            return {r.id, lbl};
        };

        if (std::holds_alternative<BaselineAuto>(sel)) {
            // Most recent fully-completed run on same branch + hostname.
            auto qs = storm::QuerySet<bench_dashboard::BenchRun>()
                              .where(storm::orm::where::field<^^bench_dashboard::BenchRun::is_full_run>() == true)
                              .where(storm::orm::where::field<^^bench_dashboard::BenchRun::branch>() ==
                                     std::string{current_branch})
                              .where(storm::orm::where::field<^^bench_dashboard::BenchRun::hostname>() ==
                                     std::string{current_host});
            return find_run(std::move(qs));
        }

        if (const auto* r = std::get_if<BaselineRunId>(&sel)) {
            auto qs = storm::QuerySet<bench_dashboard::BenchRun>().where(
                    storm::orm::where::field<^^bench_dashboard::BenchRun::id>() == static_cast<int>(r->id)
            );
            return find_run(std::move(qs));
        }

        if (const auto* b = std::get_if<BaselineBranch>(&sel)) {
            auto qs = storm::QuerySet<bench_dashboard::BenchRun>()
                              .where(storm::orm::where::field<^^bench_dashboard::BenchRun::is_full_run>() == true)
                              .where(storm::orm::where::field<^^bench_dashboard::BenchRun::branch>() == b->name);
            return find_run(std::move(qs));
        }

        return {0, ""};
    }

    // Look up the baseline real_time_ns for (run_id, test_name, dataset_size).
    // Returns nullopt when no matching row exists.
    auto lookup_baseline_ns(std::int64_t baseline_run_id, std::string const& test_name, std::int64_t dataset_size)
            -> std::optional<double> {
        auto rows = storm::QuerySet<bench_dashboard::BenchResult>()
                            .where(storm::orm::where::field<^^bench_dashboard::BenchResult::run_id>() ==
                                   static_cast<int>(baseline_run_id))
                            .where(storm::orm::where::field<^^bench_dashboard::BenchResult::test_name>() == test_name)
                            .where(storm::orm::where::field<^^bench_dashboard::BenchResult::dataset_size>() ==
                                   static_cast<int>(dataset_size))
                            .limit(1)
                            .select()
                            .execute()
                            .transform([](auto&& hive) {
                                return std::forward<decltype(hive)>(hive) | std::views::as_rvalue |
                                       std::ranges::to<std::vector<bench_dashboard::BenchResult>>();
                            });
        if (!rows || rows->empty())
            return std::nullopt;
        return rows->front().real_time_ns;
    }

    auto compute_delta(double current_ns, double baseline_ns) -> std::optional<double> {
        if (baseline_ns <= 0.0)
            return std::nullopt;
        return (current_ns - baseline_ns) / baseline_ns * 100.0;
    }

    struct BaselineComplexity {
        std::string complexity_class;
        double      complexity_coef{0.0};
    };

    // Phase 7: look up the baseline BigO row for (run_id, test_name, row_kind=="bigo").
    auto lookup_baseline_complexity(std::int64_t baseline_run_id, std::string const& test_name)
            -> std::optional<BaselineComplexity> {
        auto rows = storm::QuerySet<bench_dashboard::BenchResult>()
                            .where(storm::orm::where::field<^^bench_dashboard::BenchResult::run_id>() ==
                                   static_cast<int>(baseline_run_id))
                            .where(storm::orm::where::field<^^bench_dashboard::BenchResult::test_name>() == test_name)
                            .where(storm::orm::where::field<^^bench_dashboard::BenchResult::row_kind>() ==
                                   std::string{bench_dashboard::wire::kRowKindBigO})
                            .limit(1)
                            .select()
                            .execute()
                            .transform([](auto&& hive) {
                                return std::forward<decltype(hive)>(hive) | std::views::as_rvalue |
                                       std::ranges::to<std::vector<bench_dashboard::BenchResult>>();
                            });
        if (!rows || rows->empty())
            return std::nullopt;
        return BaselineComplexity{rows->front().complexity_class, rows->front().complexity_coef};
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

    // -----------------------------------------------------------------------
    // DashboardDB — Storm-side writer
    //
    // Inline here (not a .cppm) so the textual include of models.hpp stays in
    // the same TU as `import storm;`. Cross-BMI annotation transport on
    // clang-p2996 is unreliable (memory feedback_cpp26_module_reflection_annotations);
    // keeping the QuerySet calls and the model definitions in one TU sidesteps
    // that entirely.
    // -----------------------------------------------------------------------

    auto run_capture(char const* cmd) -> std::string {
        std::unique_ptr<std::FILE, decltype(&pclose)> pipe{::popen(cmd, "r"), &::pclose};
        if (!pipe)
            return {};
        std::string out;
        char        buf[256];
        if (std::fgets(buf, sizeof(buf), pipe.get()) != nullptr)
            out.assign(buf);
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
            out.pop_back();
        return out;
    }

    auto current_iso8601() -> std::string {
        const auto now = std::chrono::system_clock::now();
        const auto t   = std::chrono::system_clock::to_time_t(now);
        std::tm    tm_utc{};
        ::gmtime_r(&t, &tm_utc);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
        return std::string{buf};
    }

    class DashboardDB {
      public:
        [[nodiscard]] auto insert_run(std::string_view filter, bool is_full_run)
                -> std::expected<int64_t, std::string> {
            ensure_host_context();

            bench_dashboard::BenchRun row{};
            row.git_hash    = host_.git_hash;
            row.branch      = host_.branch;
            row.timestamp   = current_iso8601();
            row.hostname    = host_.hostname;
            row.compiler    = host_.compiler;
            row.filter      = std::string{filter};
            row.is_full_run = is_full_run;

            auto rc = storm::QuerySet<bench_dashboard::BenchRun>().insert(row).execute();
            if (!rc)
                return std::unexpected(std::string{rc.error().message()});
            return *rc;
        }

        auto insert_result(int64_t run_id, bench_dashboard::wire::ResultMsg const& m)
                -> std::expected<void, std::string> {
            bench_dashboard::BenchResult row{};
            row.run_id           = static_cast<int>(run_id);
            row.test_name        = m.test_name;
            row.category         = m.category;
            row.dataset_size     = static_cast<int>(m.dataset_size);
            row.row_kind         = m.row_kind;
            row.real_time_ns     = m.real_ns;
            row.cpu_time_ns      = m.cpu_ns;
            row.iterations       = static_cast<int>(m.iterations);
            row.items_per_second = m.items_per_second;
            row.complexity_class = m.complexity_class;
            row.complexity_coef  = m.complexity_coef;
            row.rms_pct          = m.rms_pct;

            storm::QuerySet<bench_dashboard::BenchResult> qs;
            auto rc = qs.template insert<storm::orm::statements::ReturnId::No>(row).execute();
            if (!rc)
                return std::unexpected(std::string{rc.error().message()});
            return {};
        }

      private:
        struct HostContext {
            std::string git_hash;
            std::string branch;
            std::string hostname;
            std::string compiler;
        };

        auto ensure_host_context() -> void {
            if (host_initialised_)
                return;
            host_initialised_ = true;
            host_.git_hash    = run_capture("git rev-parse --short HEAD 2>/dev/null");
            host_.branch      = run_capture("git rev-parse --abbrev-ref HEAD 2>/dev/null");
            host_.hostname    = run_capture("hostname -s 2>/dev/null");
            host_.compiler    = std::format("Clang {}.{}", __clang_major__, __clang_minor__);
        }

        HostContext host_{};
        bool        host_initialised_{false};
    };

    // -----------------------------------------------------------------------
    // Event loop
    // -----------------------------------------------------------------------

    // Apply one parsed wire message to TUI state + DB. Returns false on a
    // fatal DB error so the caller can bail. Soft errors (e.g. a duplicate
    // result row) are surfaced on stderr but do not stop the dashboard.
    auto apply_event(
            bench_dashboard::wire::ResultMsg const& msg,
            bench_dashboard::tui::DashboardState&   state,
            DashboardDB&                            db,
            int64_t&                                active_run_id
    ) -> bool {
        using bench_dashboard::wire::MessageKind;
        switch (msg.kind) {
        case MessageKind::RunStart: {
            auto rc = db.insert_run(msg.filter, msg.is_full_run);
            if (!rc) {
                std::fprintf(stderr, "\nstorm_bench_dashboard: insert_run failed: %s\n", rc.error().c_str());
                return false;
            }
            active_run_id  = *rc;
            auto& sess     = bench_dashboard::tui::open_session(state, msg.filter, msg.is_full_run);
            sess.run_id    = active_run_id;
            sess.timestamp = current_iso8601();
            return true;
        }
        case MessageKind::Result: {
            if (active_run_id == 0) {
                // Result without preceding run_start — open a synthetic
                // session so we don't drop data. Treats the orphan stream
                // as a single full-run session.
                auto rc = db.insert_run(msg.filter, /*is_full_run=*/true);
                if (!rc) {
                    std::fprintf(
                            stderr, "\nstorm_bench_dashboard: synthetic insert_run failed: %s\n", rc.error().c_str()
                    );
                    return false;
                }
                active_run_id = *rc;
                auto& sess    = bench_dashboard::tui::open_session(state, "", true);
                sess.run_id   = active_run_id;
            }

            auto enriched = msg;

            if (msg.row_kind == bench_dashboard::wire::kRowKindMeasurement || msg.row_kind.empty()) {
                // Phase 6: attach per-size delta for measurement rows.
                if (state.baseline_run_id != 0) {
                    enriched.baseline_looked_up = true;
                    if (auto base_ns = lookup_baseline_ns(state.baseline_run_id, msg.test_name, msg.dataset_size);
                        base_ns.has_value()) {
                        enriched.delta_pct = compute_delta(msg.real_ns, *base_ns);
                    }
                }
            } else if (msg.row_kind == bench_dashboard::wire::kRowKindBigO) {
                // Phase 7: attach baseline complexity for BigO rows.
                if (state.baseline_run_id != 0) {
                    enriched.baseline_looked_up = true;
                    if (auto base = lookup_baseline_complexity(state.baseline_run_id, msg.test_name);
                        base.has_value()) {
                        enriched.baseline_class   = base->complexity_class;
                        enriched.baseline_coef    = base->complexity_coef;
                        enriched.shape_regression = (msg.complexity_class != base->complexity_class);
                    }
                }
            }

            if (auto rc = db.insert_result(active_run_id, enriched); !rc) {
                std::fprintf(stderr, "\nstorm_bench_dashboard: insert_result failed: %s\n", rc.error().c_str());
            }
            if (!state.sessions.empty())
                bench_dashboard::tui::add_result(state.sessions.front(), enriched, state.regression_threshold);
            return true;
        }
        case MessageKind::RunComplete:
            bench_dashboard::tui::mark_complete(state);
            active_run_id = 0;
            return true;
        }
        return true;
    }

    // Refresh path: rebuild TUI state from the SQLite store. Proves the DB
    // layer is honest — `r` should reproduce what was streamed.
    auto rebuild_state_from_db(bench_dashboard::tui::DashboardState& state) -> void {
        state.sessions.clear();
        // sessions.clear() drops all Session objects including their per-session
        // counters; both sessions and counters are re-accumulated below.

        // Move elements out of the plf::hive into a vector to get a stable
        // ORDER BY sequence — hive iterates in memory-layout order, not SQL
        // order. as_rvalue casts each element to rvalue so string members are
        // moved rather than copied. DESC gives newest-first directly.
        auto runs = storm::QuerySet<bench_dashboard::BenchRun>()
                            .order_by<^^bench_dashboard::BenchRun::id, false>()
                            .select()
                            .execute()
                            .transform([](auto&& hive) {
                                return std::forward<decltype(hive)>(hive) | std::views::as_rvalue |
                                       std::ranges::to<std::vector<bench_dashboard::BenchRun>>();
                            });
        if (!runs)
            return;

        for (auto& run : *runs) {
            bench_dashboard::tui::Session sess{};
            sess.filter      = std::move(run.filter);
            sess.timestamp   = std::move(run.timestamp);
            sess.is_full_run = run.is_full_run;
            sess.complete    = true;                   // historical rows are always complete
            sess.expanded    = state.sessions.empty(); // newest (first pushed) expanded
            sess.run_id      = run.id;

            auto rows = storm::QuerySet<bench_dashboard::BenchResult>()
                                .where(storm::orm::where::field<^^bench_dashboard::BenchResult::run_id>() == run.id)
                                .order_by<^^bench_dashboard::BenchResult::id>()
                                .select()
                                .execute()
                                .transform([](auto&& hive) {
                                    return std::forward<decltype(hive)>(hive) | std::views::as_rvalue |
                                           std::ranges::to<std::vector<bench_dashboard::BenchResult>>();
                                });
            if (rows) {
                for (auto& r : *rows) {
                    bench_dashboard::wire::ResultMsg m{};
                    m.kind             = bench_dashboard::wire::MessageKind::Result;
                    m.test_name        = std::move(r.test_name);
                    m.category         = std::move(r.category);
                    m.dataset_size     = r.dataset_size;
                    m.row_kind         = std::move(r.row_kind);
                    m.real_ns          = r.real_time_ns;
                    m.cpu_ns           = r.cpu_time_ns;
                    m.iterations       = r.iterations;
                    m.items_per_second = r.items_per_second;
                    m.complexity_class = std::move(r.complexity_class);
                    m.complexity_coef  = r.complexity_coef;
                    m.rms_pct          = r.rms_pct;

                    if (m.row_kind == bench_dashboard::wire::kRowKindMeasurement || m.row_kind.empty()) {
                        if (state.baseline_run_id != 0) {
                            m.baseline_looked_up = true;
                            if (auto base_ns = lookup_baseline_ns(state.baseline_run_id, m.test_name, m.dataset_size);
                                base_ns.has_value()) {
                                m.delta_pct = compute_delta(m.real_ns, *base_ns);
                            }
                        }
                        ++sess.result_count;
                    } else if (m.row_kind == bench_dashboard::wire::kRowKindBigO) {
                        if (state.baseline_run_id != 0) {
                            m.baseline_looked_up = true;
                            if (auto base = lookup_baseline_complexity(state.baseline_run_id, m.test_name);
                                base.has_value()) {
                                m.baseline_class   = base->complexity_class;
                                m.baseline_coef    = base->complexity_coef;
                                m.shape_regression = (m.complexity_class != base->complexity_class);
                            }
                        }
                    }

                    bench_dashboard::tui::add_result(sess, m, state.regression_threshold);
                }
            }
            state.sessions.push_back(std::move(sess));
        }
    }

} // namespace

auto main(int argc, char** argv) -> int {
    const auto opts = parse_args(argc, argv);

    // One-shot backup actions short-circuit before any TUI / DB-connection
    // setup. They must NOT install signal handlers (Ctrl-C should propagate
    // straight to gh) and must NOT call set_default_connection.
    if (opts.upload_backup)
        return upload_backup(opts);
    if (opts.restore_backup)
        return restore_backup(opts);

    install_signal_handlers();

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

    if (auto run_count = storm::QuerySet<bench_dashboard::BenchRun>().count().execute(); !run_count) {
        std::fprintf(
                stderr,
                "storm_bench_dashboard: schema check failed: %s\n",
                std::string{run_count.error().message()}.c_str()
        );
        print_missing_schema_hint(opts.db_path);
        return 2;
    }
    if (auto result_count = storm::QuerySet<bench_dashboard::BenchResult>().count().execute(); !result_count) {
        std::fprintf(
                stderr,
                "storm_bench_dashboard: schema check failed: %s\n",
                std::string{result_count.error().message()}.c_str()
        );
        print_missing_schema_hint(opts.db_path);
        return 2;
    }

    const std::string_view socket_path = opts.socket_path.empty() ? bench_dashboard::wire::default_socket_path()
                                                                  : std::string_view{opts.socket_path};

    bench_dashboard::SocketServer server;
    if (auto err = server.open(socket_path); !err.empty()) {
        std::fprintf(stderr, "storm_bench_dashboard: %s\n", err.c_str());
        return 3;
    }

    DashboardDB                          db;
    bench_dashboard::tui::DashboardState state;
    state.order_arrival        = opts.order_arrival;
    state.regression_threshold = opts.regression_threshold;
    state.complexity_threshold = opts.complexity_threshold;
    int64_t active_run_id      = 0;

    // Phase 6: resolve baseline after DB connection is open so Storm queries work.
    {
        const std::string current_branch = run_capture("git rev-parse --abbrev-ref HEAD 2>/dev/null");
        const std::string current_host   = run_capture("hostname -s 2>/dev/null");
        auto [bid, blabel]               = resolve_baseline(opts.baseline, current_branch, current_host);
        state.baseline_run_id            = bid;
        state.baseline_label             = std::move(blabel);
        if (bid == 0 && std::holds_alternative<BaselineAuto>(opts.baseline)) {
            std::fprintf(
                    stderr,
                    "storm_bench_dashboard: no baseline found for branch '%s' on host '%s' — running without "
                    "comparison\n",
                    current_branch.c_str(),
                    current_host.c_str()
            );
        }
    }

    // Pre-load history so the first frame shows past runs even before any
    // bench process connects.
    rebuild_state_from_db(state);

    bench_dashboard::tui::TerminalGuard guard;
    bench_dashboard::tui::write_full_frame(bench_dashboard::tui::render(state));

    // SocketServer doesn't expose its fd today, so stdin and the socket are
    // driven independently: read_key polls stdin for 100 ms, then recv_one(0)
    // drains all queued datagrams. A single thread suffices.
    bool        running           = true;
    std::size_t since_last_redraw = 0;
    while (running) {
        if (g_should_quit.load())
            break;
        // Stdin: short timeout so the spinner ticks roughly 10× per second.
        auto key   = bench_dashboard::tui::read_key(/*timeout_ms=*/100);
        bool dirty = false;

        switch (key.kind) {
        case bench_dashboard::tui::Key::Quit:
            running = false;
            break;
        case bench_dashboard::tui::Key::Refresh:
            rebuild_state_from_db(state);
            active_run_id       = 0;
            state.scroll_offset = 0;
            dirty               = true;
            break;
        case bench_dashboard::tui::Key::Digit:
            bench_dashboard::tui::toggle_session(state, key.digit - '0');
            dirty = true;
            break;
        case bench_dashboard::tui::Key::ScrollDown:
            ++state.scroll_offset;
            dirty = true;
            break;
        case bench_dashboard::tui::Key::ScrollUp:
            if (state.scroll_offset > 0)
                --state.scroll_offset;
            dirty = true;
            break;
        case bench_dashboard::tui::Key::None:
            break;
        }

        if (!running)
            break;

        // Drain whatever has arrived on the socket since last poll. Each
        // recv_one(0) returns one datagram or std::nullopt (no more
        // available); we loop until the queue empties.
        while (true) {
            auto datagram = server.recv_one(/*timeout_ms=*/0);
            if (!datagram)
                break;
            auto msg = bench_dashboard::wire::parse(*datagram);
            if (!msg) {
                std::fprintf(
                        stderr, "\nstorm_bench_dashboard: skipped unparseable datagram (%zu bytes)\n", datagram->size()
                );
                continue;
            }
            if (!apply_event(*msg, state, db, active_run_id))
                running = false;
            dirty = true;
        }

        // Spinner: redraw at least every ~5 ticks (~500 ms) so the active
        // session's spinner glyph rotates even when no events arrive.
        ++since_last_redraw;
        if (since_last_redraw >= 5) {
            ++state.spinner_tick;
            dirty             = true;
            since_last_redraw = 0;
        }

        if (dirty)
            bench_dashboard::tui::write_full_frame(bench_dashboard::tui::render(state));
    }

    return 0;
}
