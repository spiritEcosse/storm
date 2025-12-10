#pragma once

/**
 * Alternative timing utility with runtime enable/disable
 *
 * Usage:
 *   STORM_TRACE_TIMER("operation name");
 *
 * Control via ENABLE_TIMING_TRACE variable (true/false)
 */

#include <chrono>
#include <iostream>
#include <iomanip>

namespace storm::benchmark {
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
                      << std::setprecision(3) << (duration.count() / 1000.0) << " μs\n";
        }
    };
} // namespace storm::benchmark

// Runtime-controlled timer (can enable/disable at runtime)
#define STORM_TRACE_TIMER(label) ::storm::benchmark::ScopedTimer _timer_##__LINE__(label, ENABLE_TIMING_TRACE)
