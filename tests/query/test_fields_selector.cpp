#include <gtest/gtest.h>
#include <meta>

import storm;
import std;

#include "test_fields_models.h" // NOSONAR cpp:S954

using storm::meta::FieldSelector;
using storm::meta::selector_info;
using storm::meta::ValidSelector;

// ── The proxy carries the right member ───────────────────────────────────────
static_assert(std::meta::identifier_of(selector_info<fields::FSPerson.age>()) == "age");
static_assert(std::meta::identifier_of(selector_info<fields::FSPerson.name>()) == "name");
static_assert(std::meta::identifier_of(selector_info<fields::FSPerson.id>()) == "id");
static_assert(std::meta::identifier_of(selector_info<fields::FSPerson.age>()) != "name");

// ── The inherited Field<M> static reaches through the base ───────────────────
// Load-bearing: removing the base class breaks exactly this (mutation-verified).
static_assert(decltype(fields::FSPerson.age)::field_name_sv == "age");

// ── Dual-accept: a raw info still converts (transitional, see fields.cppm) ───
static_assert(selector_info<^^FSPerson::age>() == ^^FSPerson::age);
static_assert(selector_info<fields::FSPerson.age>() == ^^FSPerson::age);

// ── The concept distinguishes the two spellings ──────────────────────────────
static_assert(FieldSelector<std::remove_cvref_t<decltype(fields::FSPerson.age)>>);
static_assert(!FieldSelector<std::meta::info>);

// ── ValidSelector accepts BOTH spellings, and rejects a non-selector ─────────
// The widened QuerySet/aggregate/order_by methods constrain on this concept in
// the NEXT commit; asserting it here means a typo inside it fails now rather
// than one commit later, when the failure would be buried in template noise.
static_assert(ValidSelector<fields::FSPerson.age>); // the proxy
static_assert(ValidSelector<^^FSPerson::age>);      // a raw info
static_assert(!ValidSelector<42>);                  // neither spelling

// ── Relation members are excluded from the generated struct ──────────────────
// Probed structurally: `is_relation_field` is NOT re-exported through
// `import storm;` (spike finding 2), so a test TU cannot call it directly.
// A requires-expression on member access soft-fails, which is enough.
//
// CAVEAT on signal (mutation-tested). `!has_tags` is a REGRESSION guard, not a
// proof the filter runs: it cannot be made to fail. Deleting the
// `is_relation_field` skip in field_specs_for does not silently admit `tags` —
// it hard-errors at the `substitute(^^FieldRef, ...)` on the very next line,
// caught by FieldRef's OWN `requires(!is_relation_field(M))`. where::Field<M>
// carries the same constraint as a backstop if FieldRef's were ever dropped.
// Either way the assert never observes a false value. Kept because it is what a
// reader checks — but the real enforcement is the constraint, not this line.
template <typename FieldsT> constexpr bool has_tags  = requires(const FieldsT& obj) { obj.tags; };
template <typename FieldsT> constexpr bool has_title = requires(const FieldsT& obj) { obj.title; };

static_assert(has_title<decltype(fields::FSArticle)>); // a real column IS there
static_assert(!has_tags<decltype(fields::FSArticle)>); // the m2m member is NOT

// ── Field counts ─────────────────────────────────────────────────────────────
// Same caveat as above for the FSArticle count: it is a regression guard on the
// arity, not an independent check of the filter.
static_assert(storm::field_specs_for(^^FSPerson).size() == 3);
static_assert(storm::field_specs_for(^^FSArticle).size() == 2); // id, title — not tags

// ── Adding a field auto-propagates: no edit to the fields:: declaration ─────
static_assert(storm::field_specs_for(^^FSPersonExtra).size() == 4);
static_assert(std::meta::identifier_of(selector_info<fields::FSPersonExtra.salary>()) == "salary");

TEST(FieldsSelector, CompileTimeOnly) {
    SUCCEED(); // the static_asserts above are the test
}

// ── Destructuring works, and its arity is compiler-checked ───────────────────
// Caveats (documented in Task 5): positional, and arity-fragile. A structured
// binding cannot be constexpr; a plain `const auto&` to the constexpr object is
// the supported form.
TEST(FieldsSelector, DestructuringBindsByPosition) {
    const auto& [fs_id, fs_name, fs_age] = fields::FSPerson;
    static_assert(std::meta::identifier_of(selector_info<fs_id>()) == "id");
    static_assert(std::meta::identifier_of(selector_info<fs_age>()) == "age");
    SUCCEED();
}
