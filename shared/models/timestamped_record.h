#pragma once

/**
 * @file timestamped_record.h
 * @brief TimestampedRecord model + its fields:: selector proxy.
 *
 * Include AFTER `import storm;` — see models.h for why.
 */

#include <chrono>
#include <meta>
#include <string>

// Auto-timestamp model — created_at stamped on INSERT only, updated_at on
// INSERT and UPDATE. Both are std::chrono::system_clock::time_point (#209).
struct TimestampedRecord {
    [[= storm::primary]] int id{};
    std::string name;
    [[= storm::auto_create]] std::chrono::system_clock::time_point created_at{};
    [[= storm::auto_update]] std::chrono::system_clock::time_point updated_at{};
};

namespace fields {
struct TimestampedRecordT;
consteval { std::meta::define_aggregate(^^TimestampedRecordT, storm::field_specs_for(^^TimestampedRecord)); }
inline constexpr TimestampedRecordT TimestampedRecord{};
} // namespace fields
