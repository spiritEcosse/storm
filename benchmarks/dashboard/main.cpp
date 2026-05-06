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
import <memory>;
import <filesystem>;

#include "models.hpp" // textual — must follow `import storm;`
#include "wire.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <format>
#include <string>
#include <string_view>
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

    struct Options {
        std::string db_path{default_db_path()};
    };

    auto print_help() -> void {
        const std::string      default_db   = default_db_path();
        const std::string_view default_sock = bench_dashboard::wire::default_socket_path();
        std::printf(
                "storm_bench_dashboard — real-time storm_bench result dashboard\n"
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
            row.real_time_ns     = m.real_ns;
            row.cpu_time_ns      = m.cpu_ns;
            row.iterations       = static_cast<int>(m.iterations);
            row.items_per_second = m.items_per_second;

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
            if (auto rc = db.insert_result(active_run_id, msg); !rc) {
                std::fprintf(stderr, "\nstorm_bench_dashboard: insert_result failed: %s\n", rc.error().c_str());
            }
            if (!state.sessions.empty())
                bench_dashboard::tui::add_result(state.sessions.front(), msg);
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

        auto runs_rc = storm::QuerySet<bench_dashboard::BenchRun>()
                               .order_by<^^bench_dashboard::BenchRun::id>()
                               .select()
                               .execute();
        if (!runs_rc)
            return;

        // Copy into a vector to get a stable ORDER BY sequence — plf::hive
        // iteration order is memory-layout order, not insertion order, so
        // rbegin..rend on the hive doesn't reliably reverse the SQL ORDER BY.
        std::vector<bench_dashboard::BenchRun> runs(runs_rc->begin(), runs_rc->end());
        for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
            bench_dashboard::tui::Session sess{};
            sess.filter      = it->filter;
            sess.timestamp   = it->timestamp;
            sess.is_full_run = it->is_full_run;
            sess.complete    = true;                   // historical rows are always complete
            sess.expanded    = state.sessions.empty(); // newest (first pushed) expanded
            sess.run_id      = it->id;

            auto rows_rc = storm::QuerySet<bench_dashboard::BenchResult>()
                                   .where(storm::orm::where::field<^^bench_dashboard::BenchResult::run_id>() == it->id)
                                   .order_by<^^bench_dashboard::BenchResult::id>()
                                   .select()
                                   .execute();
            if (rows_rc) {
                // Same plf::hive ordering caveat as BenchRun — copy into a
                // vector to lock in the SQL ORDER BY id sequence.
                std::vector<bench_dashboard::BenchResult> rows(rows_rc->begin(), rows_rc->end());
                for (auto const& r : rows) {
                    bench_dashboard::wire::ResultMsg m{};
                    m.kind             = bench_dashboard::wire::MessageKind::Result;
                    m.test_name        = r.test_name;
                    m.category         = r.category;
                    m.dataset_size     = r.dataset_size;
                    m.real_ns          = r.real_time_ns;
                    m.cpu_ns           = r.cpu_time_ns;
                    m.iterations       = r.iterations;
                    m.items_per_second = r.items_per_second;
                    bench_dashboard::tui::add_result(sess, m);
                    ++sess.result_count;
                }
            }
            state.sessions.push_back(std::move(sess));
        }
    }

} // namespace

auto main(int argc, char** argv) -> int {
    const auto opts = parse_args(argc, argv);
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

    const std::string_view socket_path = bench_dashboard::wire::default_socket_path();

    bench_dashboard::SocketServer server;
    if (auto err = server.open(socket_path); !err.empty()) {
        std::fprintf(stderr, "storm_bench_dashboard: %s\n", err.c_str());
        return 3;
    }

    DashboardDB                          db;
    bench_dashboard::tui::DashboardState state;
    int64_t                              active_run_id = 0;

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
            active_run_id = 0;
            dirty         = true;
            break;
        case bench_dashboard::tui::Key::Digit:
            bench_dashboard::tui::toggle_session(state, key.digit - '0');
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
