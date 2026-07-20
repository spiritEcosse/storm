#include <gtest/gtest.h>

import storm;
import std;

// Compile-time-only verification of the ValidForeignKey concept (#474). Every
// static_assert is checked at TU compile time; the runtime TEST body exists only
// so the file registers with GoogleTest and the assertions are compiled.

using storm::orm::statements::ValidForeignKey;

// A valid FK target: has a primary key.
struct Related {
    [[= storm::FieldAttr::primary]] int id{};
    std::string                         name;
};

// An FK target lacking a primary key — must be rejected.
struct NoPkModel {
    int a{};
    int b{};
};

// A self-referential model (hierarchy): a real `[[= storm::fk<>]] std::optional<Node>
// parent;` member is not expressible in C++ — std::optional<T> requires T complete,
// and T is still incomplete inside its own definition. ValidForeignKey only inspects
// the FK *target type* (never the member's storage), so checking Node against itself
// below exercises the identical "FK target == owning model" scenario without that
// language-level obstacle.
struct Node {
    [[= storm::FieldAttr::primary]] int id{};
};

// ---- Positive: FK whose target has a primary key -----------------------------
static_assert(ValidForeignKey<Related>);
static_assert(ValidForeignKey<std::optional<Related>>); // nullable FK unwraps to Related

// ---- Self-referential: terminates in one step, no recursion into target FKs --
static_assert(ValidForeignKey<Node>);
static_assert(ValidForeignKey<std::optional<Node>>);

// ---- Negative: FK target with no primary key must be rejected -----------------
static_assert(!ValidForeignKey<NoPkModel>);
static_assert(!ValidForeignKey<std::optional<NoPkModel>>);

TEST(ValidForeignKeyConceptTest, CompileTimeOnly) {
    // Verification is entirely in the static_asserts above; this body just gives
    // GoogleTest something to run.
    SUCCEED();
}
