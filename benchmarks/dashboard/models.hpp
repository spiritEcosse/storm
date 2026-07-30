#pragma once

// Dashboard schema — one row per storm_bench session (BenchRun) plus one row
// per individual benchmark result (BenchResult). Lives in `bench_dashboard`
// for namespace-reflection-based schema discovery via storm_enable_migrations.
//
// Textual header — must be #include'd AFTER `import storm;` so the
// [[= storm::meta::*]] annotations bind to the imported objects.
// Keeping the structs in a textual header (not a .cppm) avoids clang-p2996
// issue #262, where annotations are silently dropped across BMI boundaries
// (memory feedback_cpp26_module_reflection_annotations).

#include <meta>
#include <string>

namespace bench_dashboard {

    struct BenchRun {
        [[= storm::meta::primary]] int id{};
        std::string                    git_hash;
        std::string                    branch;
        std::string                    timestamp; // ISO 8601
        std::string                    hostname;
        std::string                    compiler;           // e.g. "Clang 21.0"
        std::string                    filter;             // gbench --benchmark_filter, empty = full run
        bool                           is_full_run{false}; // tagged so --baseline auto can skip partial runs
        bool                           is_raw{false};      // true => raw-SQLite baseline run (storm_anchors)
    };

    struct BenchResult {
        [[= storm::meta::primary]] int id{};
        [[= storm::meta::indexed]] int run_id{};  // FK → BenchRun.id (logical)
        std::string                    test_name; // e.g. "Storm/WHERE/where_int_gt"
        std::string                    category;  // e.g. "WHERE"
        int                            dataset_size{};
        std::string                    row_kind{"measurement"}; // "measurement" | "bigo" | "rms"
        double                         real_time_ns{};
        double                         cpu_time_ns{};
        int                            iterations{};
        double                         items_per_second{};
        std::string                    complexity_class;  // e.g. "N", "NlgN", "N^2" (bigo rows only)
        double                         complexity_coef{}; // leading coefficient (bigo rows only)
        double                         rms_pct{};         // fit quality 0-100 (rms rows only)
    };

    // fields:: selector proxies (#518). Inside bench_dashboard, so the spelling
    // is bench_dashboard::fields::BenchRun.branch.
    namespace fields {

        struct BenchRunT;
        consteval {
            std::meta::define_aggregate(^^BenchRunT, storm::field_specs_for(^^BenchRun));
        }
        inline constexpr BenchRunT BenchRun{};

        struct BenchResultT;
        consteval {
            std::meta::define_aggregate(^^BenchResultT, storm::field_specs_for(^^BenchResult));
        }
        inline constexpr BenchResultT BenchResult{};

    } // namespace fields

} // namespace bench_dashboard
