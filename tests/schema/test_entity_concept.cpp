#include <gtest/gtest.h>
#include <meta>

#include "test_db_helpers.h"

// NOLINTBEGIN(misc-const-correctness)

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954

// ── #472: the Entity concept is a compile-time structural gate ───────────────
// Entity<T> is true iff T is a reflectable class type. It is PURELY structural:
// any class type qualifies (models here happen to be classes); scalars, pointers,
// references, void, and functions do not. Semantic model-ness (a primary key,
// valid storage annotations, etc.) is enforced later by the ModelWithPrimaryKey /
// ModelStorageAnnotated / ModelFkPoliciesValid concepts. Entity is the outer
// structural boundary layered above those.

// A real model satisfies Entity.
static_assert(storm::meta::Entity<Person>);
static_assert(storm::meta::Entity<Message>);

// Non-class types are rejected — proves the concept actually gates.
static_assert(!storm::meta::Entity<int>);
static_assert(!storm::meta::Entity<double>);
static_assert(!storm::meta::Entity<int*>);
static_assert(!storm::meta::Entity<int&>);
static_assert(!storm::meta::Entity<void>);
static_assert(!storm::meta::Entity<void()>);

// NOLINTEND(misc-const-correctness)

TEST(EntityConcept, CompileTimeOnly) {
    // The static_asserts above are the real test; this keeps the TU a runnable
    // GTest target so the suite reports it.
    SUCCEED();
}
