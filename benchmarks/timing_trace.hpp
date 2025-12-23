#pragma once

/**
 * Timing Trace Utility for Performance Investigation
 *
 * Two modes:
 * 1. Simple scoped timer: STORM_TRACE_TIMER("label")
 * 2. Aggregating tracer: TRACE_START/END with TRACE_REPORT
 *
 * Usage for aggregating mode:
 *   #define ENABLE_TIMING_TRACE true
 *   #include "timing_trace.hpp"
 *
 *   TRACE_INIT();
 *   for (int i = 0; i < iterations; i++) {
 *       TRACE_START("sql_build");
 *       // ... code ...
 *       TRACE_END("sql_build");
 *
 *       TRACE_START("bind_params");
 *       // ... code ...
 *       TRACE_END("bind_params");
 *   }
 *   TRACE_REPORT_N(iterations);
 */

#include <chrono>
#include <iostream>
#include <iomanip>
#include <string>
#include <unordered_map>
#include <vector>

namespace storm::benchmark {

    // ========================================================================
    // Simple scoped timer (prints on destruction)
    // ========================================================================
    class ScopedTimer {
        const char*                                    label_;
        bool                                           enabled_;
        std::chrono::high_resolution_clock::time_point start_;

      public:
        explicit ScopedTimer(const char* label, bool enabled = true) : label_(label), enabled_(enabled) {
            if (enabled_) {
                start_ = std::chrono::high_resolution_clock::now();
            }
        }

        ~ScopedTimer() {
            if (!enabled_)
                return;
            auto end      = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_);
            std::cout << "[TRACE] " << std::setw(40) << std::left << label_ << ": " << std::fixed
                      << std::setprecision(3) << (duration.count() / 1000.0) << " us\n";
        }
    };

    // ========================================================================
    // Aggregating tracer (collects stats, reports at end)
    // ========================================================================
    namespace timing {

        struct TraceEntry {
            std::string name;
            uint64_t    total_ns   = 0;
            uint64_t    call_count = 0;
            uint64_t    start_time = 0;
        };

        class TraceContext {
            std::unordered_map<std::string, TraceEntry> entries_;
            std::vector<std::string>                    order_;
            bool                                        enabled_ = false;

          public:
            static TraceContext& instance() {
                static thread_local TraceContext ctx;
                return ctx;
            }

            void set_enabled(bool enabled) {
                enabled_ = enabled;
            }
            bool is_enabled() const {
                return enabled_;
            }

            void reset() {
                entries_.clear();
                order_.clear();
            }

            void start(const char* name) {
                if (!enabled_)
                    return;
                auto& entry = entries_[name];
                if (entry.name.empty()) {
                    entry.name = name;
                    order_.push_back(name);
                }
                entry.start_time = now_ns();
            }

            void end(const char* name) {
                if (!enabled_)
                    return;
                auto it = entries_.find(name);
                if (it != entries_.end()) {
                    uint64_t elapsed = now_ns() - it->second.start_time;
                    it->second.total_ns += elapsed;
                    it->second.call_count++;
                }
            }

            void report(int iterations = 1) {
                if (entries_.empty())
                    return;

                uint64_t total_all = 0;
                for (const auto& [_, entry] : entries_) {
                    total_all += entry.total_ns;
                }

                std::cout << "\n╔═══════════════════════════════════════════════════════════════════════╗\n";
                std::cout << "║                        TIMING TRACE REPORT                            ║\n";
                std::cout << "╠═════════════════════════╦════════════╦════════════╦══════════╦════════╣\n";
                std::cout << "║ " << std::left << std::setw(23) << "Section"
                          << " ║ " << std::right << std::setw(10) << "Total(us)"
                          << " ║ " << std::setw(10) << "Calls"
                          << " ║ " << std::setw(8) << "Avg(ns)"
                          << " ║ " << std::setw(6) << "%" << " ║\n";
                std::cout << "╠═════════════════════════╬════════════╬════════════╬══════════╬════════╣\n";

                for (const auto& name : order_) {
                    const auto& entry  = entries_.at(name);
                    double      pct    = (total_all > 0) ? (100.0 * entry.total_ns / total_all) : 0;
                    double      avg_ns = (entry.call_count > 0) ? (double)entry.total_ns / entry.call_count : 0;

                    std::cout << "║ " << std::left << std::setw(23) << entry.name << " ║ " << std::right
                              << std::setw(10) << (entry.total_ns / 1000) << " ║ " << std::setw(10) << entry.call_count
                              << " ║ " << std::setw(8) << std::fixed << std::setprecision(0) << avg_ns << " ║ "
                              << std::setw(5) << std::fixed << std::setprecision(1) << pct << "% ║\n";
                }

                std::cout << "╠═════════════════════════╬════════════╬════════════╬══════════╬════════╣\n";
                std::cout << "║ " << std::left << std::setw(23) << "TOTAL"
                          << " ║ " << std::right << std::setw(10) << (total_all / 1000) << " ║ " << std::setw(10)
                          << iterations << " ║ " << std::setw(8) << std::fixed << std::setprecision(0)
                          << ((double)total_all / iterations) << " ║ " << std::setw(5) << "100.0% ║\n";
                std::cout << "╚═════════════════════════╩════════════╩════════════╩══════════╩════════╝\n\n";
            }

          private:
            static uint64_t now_ns() {
                return std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::high_resolution_clock::now().time_since_epoch()
                )
                        .count();
            }
        };

    } // namespace timing

} // namespace storm::benchmark

// ========================================================================
// Macros
// ========================================================================

// Simple scoped timer
#define STORM_TRACE_TIMER(label) ::storm::benchmark::ScopedTimer _timer_##__LINE__(label, ENABLE_TIMING_TRACE)

// Aggregating tracer macros
#define TRACE_INIT()                                                                                                   \
    do {                                                                                                               \
        ::storm::benchmark::timing::TraceContext::instance().reset();                                                  \
        ::storm::benchmark::timing::TraceContext::instance().set_enabled(ENABLE_TIMING_TRACE);                         \
    } while (0)

#define TRACE_START(name) ::storm::benchmark::timing::TraceContext::instance().start(name)

#define TRACE_END(name) ::storm::benchmark::timing::TraceContext::instance().end(name)

#define TRACE_REPORT() ::storm::benchmark::timing::TraceContext::instance().report()

#define TRACE_REPORT_N(n) ::storm::benchmark::timing::TraceContext::instance().report(n)
