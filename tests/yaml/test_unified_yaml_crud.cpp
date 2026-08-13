// Issue #561: the unified YAML corpus is split by category across one TU per
// dispatch family so its per-case template instantiation compiles in parallel.
// This TU owns the write family (insert / update / erase).
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage) - #embed needs a literal filename
#define STORM_UNIFIED_CASES_FILE "test_cases/unified_cases_crud.json"

#include <gtest/gtest.h>
#include "test_db_helpers.h"

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954
#include "test_seed_helpers.h"
#include "test_select_runner.h"
#include "test_write_runner.h"
#include "test_yaml_register.h"
#include "test_parser.hpp"
#include "test_unified_yaml_body.h"

STORM_REGISTER_UNIFIED_YAML_SUITE(Crud, "UnifiedYamlCrudTest")
