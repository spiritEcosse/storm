#include <gtest/gtest.h>
#include <meta>
#include <plf_hive/plf_hive.h> // NOSONAR cpp:S954 — must precede `import std;` (see test_m2m_models.h)

#include "test_db_helpers.h"

// This TU supplies the SQLite half of test_composite_m2m_junction_body.h;
// test_composite_m2m_junction_pg.cpp supplies the other half. See that header's own comment
// for the compile-time-TU-split rationale.
#define STORM_SPLIT_TYPES DatabaseTypesSqliteHalf
#define STORM_SPLIT_TYPE_NAMES DatabaseTypesSqliteHalfNames
#include "test_composite_m2m_junction_body.h"
#undef STORM_SPLIT_TYPE_NAMES
#undef STORM_SPLIT_TYPES
