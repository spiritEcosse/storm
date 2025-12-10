#pragma once

/**
 * Performance profiling utilities for Storm ORM
 *
 * Usage:
 *   1. Add to code: STORM_TRACE("operation name");
 *   2. Build with: -DSTORM_ENABLE_TIMING_TRACE
 *   3. Outputs microsecond-precision timings to stdout
 *
 * Example:
 *   void expensive_function() {
 *       STORM_TRACE("expensive_function");
 *       // ... code ...
 *   }
 */

#include <chrono>
#include <iostream>
#include <iomanip>

#ifdef STORM_ENABLE_TIMING_TRACE

namespace storm::orm::profiling {
    class ScopedTimer {
        const char*                                    label_;
        std::chrono::high_resolution_clock::time_point start_;

      public:
        explicit ScopedTimer(const char* label) : label_(label) {
            start_ = std::chrono::high_resolution_clock::now();
        }

        ~ScopedTimer() {
            auto end      = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_);
            std::cout << "[TRACE] " << std::setw(40) << std::left << label_ << ": " << std::fixed
                      << std::setprecision(3) << (duration.count() / 1000.0) << " μs\n";
        }
    };
} // namespace storm::orm::profiling

// Macro helpers for unique variable names
#define STORM_TRACE_CONCAT(a, b) a##b
#define STORM_TRACE_UNIQUE(prefix, line) STORM_TRACE_CONCAT(prefix, line)
#define STORM_TRACE(label) ::storm::orm::profiling::ScopedTimer STORM_TRACE_UNIQUE(_storm_timer_, __LINE__)(label)
#define STORM_TRACE_START() std::cout << "\n=== STORM TRACE START ===\n"
#define STORM_TRACE_END() std::cout << "=== STORM TRACE END ===\n\n"

#else

// No-op macros when timing is disabled
#define STORM_TRACE(label) ((void)0)
#define STORM_TRACE_START() ((void)0)
#define STORM_TRACE_END() ((void)0)

#endif // STORM_ENABLE_TIMING_TRACE
