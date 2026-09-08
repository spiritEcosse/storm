#pragma once

/**
 * @file test_models.h
 * @brief Umbrella over the shared models plus the test-side helpers.
 *
 * PREFER THE NARROW HEADERS (issue #634) — this umbrella parses all twelve
 * shared models, both seed arrays and every helper, which a TU that touches one
 * of them pays for in full. Include only what a TU uses:
 *
 *   "test_fixture.h"                  StormTestFixture, ensure_table(s) — no models
 *   "../shared/models/person.h"       one header per model, each with its fields:: proxy
 *   "../shared/models/people_25.h"    the seed arrays, separately from the models
 *   "test_record_helpers.h"           make_record/make_updated_record/is_original_record
 *   "test_join_data.h"                populate_join_test_data
 *
 * IMPORTANT: Include this file AFTER `import storm;` in each .cpp that uses it.
 * The [[= storm::*]] attributes require the storm module to be imported before
 * these structs are compiled.
 *
 * Usage in test files:
 *   import storm;
 *   #include "test_models.h"  // AFTER import
 */

#include "../shared/models.h"
#include "test_fixture.h"
#include "test_join_data.h"
#include "test_record_helpers.h"
