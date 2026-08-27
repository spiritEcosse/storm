#include <gtest/gtest.h>
#include "test_db_helpers.h"

// This TU supplies the PostgreSQL half of test_many_to_many_body.h;
// test_many_to_many_sqlite.cpp supplies the other half. See that header's own comment
// for the compile-time-TU-split rationale.
#define STORM_SPLIT_TYPES DatabaseTypesPgHalf
#define STORM_SPLIT_TYPE_NAMES DatabaseTypesPgHalfNames
#include "test_many_to_many_body.h"
#undef STORM_SPLIT_TYPE_NAMES
#undef STORM_SPLIT_TYPES
