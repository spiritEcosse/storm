#pragma once

/**
 * @file models.h
 * @brief Umbrella over the shared model structs used by tests and benchmarks.
 *
 * PREFER THE PER-MODEL HEADERS in shared/models/ — this umbrella parses all
 * twelve models and both seed arrays, which a TU that touches one of them pays
 * for in full (issue #634). Include it only from a TU that genuinely wants
 * everything.
 *
 * IMPORTANT: Include this file (or any header in shared/models/) AFTER
 * `import storm;` — the [[= storm::*]] attributes require the storm module, and
 * the fields:: blocks call storm::field_specs_for, a *function*, which is a
 * harder dependency than an annotation. Including one too early fails inside a
 * consteval block rather than at the attribute.
 *
 * Each header carries its own model, that model's fields:: selector proxy, and
 * includes the models it references (Message -> Person, and so on), so any one
 * of them can be included on its own.
 */

#include "models/cascade_child.h"
#include "models/color.h"
#include "models/extended_types.h"
#include "models/message.h"
#include "models/messages_8.h"
#include "models/people_25.h"
#include "models/person.h"
#include "models/restrict_child.h"
#include "models/set_null_child.h"
#include "models/simple_record.h"
#include "models/task.h"
#include "models/timestamped_record.h"
#include "models/uuid_pk_model.h"
#include "models/uuid_pk_ref.h"
