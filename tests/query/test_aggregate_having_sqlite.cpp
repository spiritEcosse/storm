#include <gtest/gtest.h>
#include "test_db_helpers.h"

// This TU supplies the SQLite half of test_aggregate_having_body.h;
// test_aggregate_having_pg.cpp supplies the other half. See that header's own comment
// for the compile-time-TU-split rationale.
#define STORM_SPLIT_TYPES DatabaseTypesSqliteHalf
#define STORM_SPLIT_TYPE_NAMES DatabaseTypesSqliteHalfNames
#include "test_aggregate_having_body.h"
#undef STORM_SPLIT_TYPE_NAMES
#undef STORM_SPLIT_TYPES
