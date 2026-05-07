#pragma once

// DB writer + event handlers for storm_bench_dashboard (Issue #247).
//
// Textual header — `#include`d from the anonymous namespace of main.cpp so
// the textual `models.hpp` include + `import storm;` from main.cpp's TU stay
// in scope. Cross-BMI annotation transport on clang-p2996 is unreliable
// (memory feedback_cpp26_module_reflection_annotations); inlining all
// QuerySet calls here keeps the model definitions and the queries in the
// same TU. Split out of main.cpp purely to keep each TU under the 600-line
// code-quality cap. Do NOT include this from any other TU.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

    // -----------------------------------------------------------------------
    // Phase 6: baseline resolution + per-result delta lookup
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

    // The plf::hive returned by .select().execute() doesn't iterate in SQL
    // ORDER BY order; we have to materialise into a vector to get a stable
    // sequence and to move strings out instead of copying. Same `transform`
    // appears across resolve_baseline / lookup_baseline_* / rebuild_state — a
    // single point of definition keeps the SonarCloud "no duplicates" gate
    // happy and the call sites readable.
    template <typename T> auto hive_to_vector_lambda() {
        return [](auto&& hive) {
            return std::forward<decltype(hive)>(hive) | std::views::as_rvalue | std::ranges::to<std::vector<T>>();
        };
    }

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
                                .transform(hive_to_vector_lambda<bench_dashboard::BenchRun>());
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

    // Run the QuerySet's terminal `.select().execute()` chain and return the
    // hive contents as a vector, or an empty vector on query failure.
    // Single source of truth for the "execute and materialise" pattern.
    template <typename T, typename QS> auto materialise_select(QS&& qs) -> std::vector<T> {
        auto rows = std::forward<QS>(qs).select().execute().transform(hive_to_vector_lambda<T>());
        return rows ? std::move(*rows) : std::vector<T>{};
    }

    // Run the supplied query-set extension through .limit(1).select().execute()
    // and materialise into a vector. Centralises the "fetch at most one row"
    // chain that lookup_baseline_ns / lookup_baseline_complexity both need.
    template <typename QS> auto fetch_first_result_row(QS&& qs) -> std::vector<bench_dashboard::BenchResult> {
        return materialise_select<bench_dashboard::BenchResult>(std::forward<QS>(qs).limit(1));
    }

    // Look up the baseline real_time_ns for (run_id, test_name, dataset_size).
    // Returns nullopt when no matching row exists.
    auto lookup_baseline_ns(std::int64_t baseline_run_id, std::string const& test_name, std::int64_t dataset_size)
            -> std::optional<double> {
        auto rows = fetch_first_result_row(
                storm::QuerySet<bench_dashboard::BenchResult>()
                        .where(storm::orm::where::field<^^bench_dashboard::BenchResult::run_id>() ==
                               static_cast<int>(baseline_run_id))
                        .where(storm::orm::where::field<^^bench_dashboard::BenchResult::test_name>() == test_name)
                        .where(storm::orm::where::field<^^bench_dashboard::BenchResult::dataset_size>() ==
                               static_cast<int>(dataset_size))
        );
        if (rows.empty())
            return std::nullopt;
        return rows.front().real_time_ns;
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
        auto rows = fetch_first_result_row(
                storm::QuerySet<bench_dashboard::BenchResult>()
                        .where(storm::orm::where::field<^^bench_dashboard::BenchResult::run_id>() ==
                               static_cast<int>(baseline_run_id))
                        .where(storm::orm::where::field<^^bench_dashboard::BenchResult::test_name>() == test_name)
                        .where(storm::orm::where::field<^^bench_dashboard::BenchResult::row_kind>() ==
                               std::string{bench_dashboard::wire::kRowKindBigO})
        );
        if (rows.empty())
            return std::nullopt;
        return BaselineComplexity{rows.front().complexity_class, rows.front().complexity_coef};
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

    class DashboardDB {
      public:
        [[nodiscard]] auto insert_run(std::string_view filter, bool is_full_run)
                -> std::expected<std::int64_t, std::string> {
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

        auto insert_result(std::int64_t run_id, bench_dashboard::wire::ResultMsg const& m)
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
    // Event loop helpers
    // -----------------------------------------------------------------------

    // Phase 6: attach per-size delta to a measurement row when a baseline
    // is active. No-op when baseline_run_id == 0.
    auto enrich_measurement(bench_dashboard::wire::ResultMsg& msg, std::int64_t baseline_run_id) -> void {
        if (baseline_run_id == 0)
            return;
        msg.baseline_looked_up = true;
        if (auto base_ns = lookup_baseline_ns(baseline_run_id, msg.test_name, msg.dataset_size); base_ns.has_value())
            msg.delta_pct = compute_delta(msg.real_ns, *base_ns);
    }

    // Phase 7: attach baseline complexity class + coef to a BigO row when a
    // baseline is active. No-op when baseline_run_id == 0.
    auto enrich_bigo(bench_dashboard::wire::ResultMsg& msg, std::int64_t baseline_run_id) -> void {
        if (baseline_run_id == 0)
            return;
        msg.baseline_looked_up = true;
        if (auto base = lookup_baseline_complexity(baseline_run_id, msg.test_name); base.has_value()) {
            msg.baseline_class   = base->complexity_class;
            msg.baseline_coef    = base->complexity_coef;
            msg.shape_regression = (msg.complexity_class != base->complexity_class);
        }
    }

    // Dispatcher: pick the right per-row enrichment based on row_kind.
    auto enrich_with_baseline(bench_dashboard::wire::ResultMsg& msg, std::int64_t baseline_run_id) -> void {
        if (msg.row_kind == bench_dashboard::wire::kRowKindMeasurement || msg.row_kind.empty())
            enrich_measurement(msg, baseline_run_id);
        else if (msg.row_kind == bench_dashboard::wire::kRowKindBigO)
            enrich_bigo(msg, baseline_run_id);
    }

    // RunStart handler. Returns false on fatal DB error.
    auto handle_run_start(
            bench_dashboard::wire::ResultMsg const& msg,
            bench_dashboard::tui::DashboardState&   state,
            DashboardDB&                            db,
            std::int64_t&                           active_run_id
    ) -> bool {
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

    // Open a synthetic full-run session for a Result that arrived before any
    // RunStart. Returns false on fatal DB error.
    auto open_synthetic_session(
            bench_dashboard::wire::ResultMsg const& msg,
            bench_dashboard::tui::DashboardState&   state,
            DashboardDB&                            db,
            std::int64_t&                           active_run_id
    ) -> bool {
        auto rc = db.insert_run(msg.filter, /*is_full_run=*/true);
        if (!rc) {
            std::fprintf(stderr, "\nstorm_bench_dashboard: synthetic insert_run failed: %s\n", rc.error().c_str());
            return false;
        }
        active_run_id = *rc;
        auto& sess    = bench_dashboard::tui::open_session(state, "", true);
        sess.run_id   = active_run_id;
        return true;
    }

    // Result handler. Opens a synthetic session if needed, enriches the row
    // with baseline data, persists it, and pushes it into the active session.
    auto handle_result(
            bench_dashboard::wire::ResultMsg const& msg,
            bench_dashboard::tui::DashboardState&   state,
            DashboardDB&                            db,
            std::int64_t&                           active_run_id
    ) -> bool {
        if (active_run_id == 0 && !open_synthetic_session(msg, state, db, active_run_id))
            return false;

        auto enriched = msg;
        enrich_with_baseline(enriched, state.baseline_run_id);

        if (auto rc = db.insert_result(active_run_id, enriched); !rc)
            std::fprintf(stderr, "\nstorm_bench_dashboard: insert_result failed: %s\n", rc.error().c_str());
        if (!state.sessions.empty())
            bench_dashboard::tui::add_result(state.sessions.front(), enriched, state.regression_threshold);
        return true;
    }

    // Apply one parsed wire message to TUI state + DB. Returns false on a
    // fatal DB error so the caller can bail. Soft errors (e.g. a duplicate
    // result row) are surfaced on stderr but do not stop the dashboard.
    auto apply_event(
            bench_dashboard::wire::ResultMsg const& msg,
            bench_dashboard::tui::DashboardState&   state,
            DashboardDB&                            db,
            std::int64_t&                           active_run_id
    ) -> bool {
        using bench_dashboard::wire::MessageKind;
        switch (msg.kind) {
        case MessageKind::RunStart:
            return handle_run_start(msg, state, db, active_run_id);
        case MessageKind::Result:
            return handle_result(msg, state, db, active_run_id);
        case MessageKind::RunComplete:
            bench_dashboard::tui::mark_complete(state);
            active_run_id = 0;
            return true;
        }
        return true;
    }

    // Map a stored BenchResult row → wire::ResultMsg. Strings are moved out
    // of `r` (caller's iteration owns the row).
    auto build_result_msg_from_row(bench_dashboard::BenchResult& r) -> bench_dashboard::wire::ResultMsg {
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
        return m;
    }

    // Build a Session from a stored BenchRun + its already-known position.
    // `is_first` controls whether this session opens expanded (newest only).
    auto build_session_from_run(bench_dashboard::BenchRun& run, bool is_first) -> bench_dashboard::tui::Session {
        bench_dashboard::tui::Session sess{};
        sess.filter      = std::move(run.filter);
        sess.timestamp   = std::move(run.timestamp);
        sess.is_full_run = run.is_full_run;
        sess.complete    = true;     // historical rows are always complete
        sess.expanded    = is_first; // newest (first pushed) expanded
        sess.run_id      = run.id;
        return sess;
    }

    // Pull every BenchResult row for `run_id` (in insert order) into a vector.
    // Returns an empty vector on query failure rather than propagating the error.
    auto load_results_for_run(int run_id) -> std::vector<bench_dashboard::BenchResult> {
        return materialise_select<bench_dashboard::BenchResult>(
                storm::QuerySet<bench_dashboard::BenchResult>()
                        .where(storm::orm::where::field<^^bench_dashboard::BenchResult::run_id>() == run_id)
                        .order_by<^^bench_dashboard::BenchResult::id>()
        );
    }

    // Replay every stored BenchResult into `sess`, incrementing measurement
    // counters and applying baseline enrichment (mirrors the live path).
    auto replay_run_results_into_session(
            bench_dashboard::tui::Session&             sess,
            std::vector<bench_dashboard::BenchResult>  rows,
            std::int64_t                               baseline_run_id,
            double                                     regression_threshold
    ) -> void {
        for (auto& r : rows) {
            auto m = build_result_msg_from_row(r);
            enrich_with_baseline(m, baseline_run_id);
            if (m.row_kind == bench_dashboard::wire::kRowKindMeasurement || m.row_kind.empty())
                ++sess.result_count;
            bench_dashboard::tui::add_result(sess, m, regression_threshold);
        }
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
                            .transform(hive_to_vector_lambda<bench_dashboard::BenchRun>());
        if (!runs)
            return;

        for (auto& run : *runs) {
            auto sess = build_session_from_run(run, /*is_first=*/state.sessions.empty());
            replay_run_results_into_session(
                    sess, load_results_for_run(run.id), state.baseline_run_id, state.regression_threshold
            );
            state.sessions.push_back(std::move(sess));
        }
    }

} // namespace
