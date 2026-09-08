#pragma once

/**
 * @file simple_record.h
 * @brief SimpleRecord model + its fields:: selector proxy.
 *
 * Include AFTER `import storm;` — see models.h for why.
 */

#include <meta>
#include <string>

// Shared simple record — covers batch/transaction/update/reset tests needing {id, name, value}.
struct SimpleRecord {
    [[= storm::primary]] int id{};
    std::string name;
    int value{};
};

namespace fields {
struct SimpleRecordT;
consteval { std::meta::define_aggregate(^^SimpleRecordT, storm::field_specs_for(^^SimpleRecord)); }
inline constexpr SimpleRecordT SimpleRecord{};
} // namespace fields
