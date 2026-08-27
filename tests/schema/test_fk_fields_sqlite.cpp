#include <gtest/gtest.h>
#include "test_db_helpers.h"

// This TU supplies the SQLite half of test_fk_fields_body.h;
// test_fk_fields_pg.cpp supplies the other half. See that header's own comment
// for the compile-time-TU-split rationale.
#define STORM_SPLIT_TYPES DatabaseTypesSqliteHalf
#define STORM_SPLIT_TYPE_NAMES DatabaseTypesSqliteHalfNames
#include "test_fk_fields_body.h"
#undef STORM_SPLIT_TYPE_NAMES
#undef STORM_SPLIT_TYPES
