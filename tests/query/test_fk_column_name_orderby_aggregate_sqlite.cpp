// test_fk_column_name_orderby_aggregate.cpp — #570
//
// ORDER BY and COUNT(DISTINCT) on an FK member must emit the FK's real column
// name (<member>_id), not the bare member identifier. Before the fix both sites
// spelled std::meta::identifier_of(member) directly instead of routing through
// the canonical column-name writer meta::append_column_name (#422), so
// `order_by<fields::Message.sender>()` emitted `ORDER BY sender` — a column that
// does not exist (the real one is `sender_id`) — and the statement failed to
// prepare at RUNTIME. Unlike #542 this was live and user-reachable: nothing gates
// it, since an FK member is an ordinary FieldRef<M> column proxy (#518).
//
// Tested by EXECUTION on both backends, not only by SQL text: SQLite and PG
// disagree about what they tolerate (cf. #536, #519), and a text-only assertion
// would not have caught the prepare failure.

#include <gtest/gtest.h>
#include <meta>
#include <plf_hive/plf_hive.h> // NOSONAR cpp:S954 — must precede `import std;` (see test_m2m_models.h)

#include "test_db_helpers.h"

// This TU supplies the SQLite half of test_fk_column_name_orderby_aggregate_body.h;
// test_fk_column_name_orderby_aggregate_pg.cpp supplies the other half. See that header's own comment
// for the compile-time-TU-split rationale.
#define STORM_SPLIT_TYPES DatabaseTypesSqliteHalf
#define STORM_SPLIT_TYPE_NAMES DatabaseTypesSqliteHalfNames
#include "test_fk_column_name_orderby_aggregate_body.h"
#undef STORM_SPLIT_TYPE_NAMES
#undef STORM_SPLIT_TYPES
