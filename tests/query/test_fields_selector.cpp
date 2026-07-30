#include <gtest/gtest.h>
#include <meta>

import storm;
import std;

#include "test_fields_models.h" // NOSONAR cpp:S954

using storm::meta::FieldSelector;
using storm::meta::RelationSelector;
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

// ── Relation members ARE present, but as a DIFFERENT proxy type ──────────────
// A relation member (m2m, reverse_fk) is not a column, but it IS a legal join
// target — `join<fields::Article.tags>()` has to work. So field_specs_for emits
// RelationRef for it instead of FieldRef. RelationRef deliberately has NO
// where::Field<M> base, which is what keeps a relation out of a WHERE clause
// (the #408 guarantee) while still naming it for a join.
template <typename FieldsT> constexpr bool has_tags  = requires(const FieldsT& obj) { obj.tags; };
template <typename FieldsT> constexpr bool has_title = requires(const FieldsT& obj) { obj.title; };

static_assert(has_title<decltype(fields::FSArticle)>); // a real column
static_assert(has_tags<decltype(fields::FSArticle)>);  // the m2m member, as RelationRef

// The two proxies are distinct types, and only the column one is a FieldSelector.
static_assert(FieldSelector<std::remove_cvref_t<decltype(fields::FSArticle.title)>>);
static_assert(!FieldSelector<std::remove_cvref_t<decltype(fields::FSArticle.tags)>>);
static_assert(RelationSelector<std::remove_cvref_t<decltype(fields::FSArticle.tags)>>);
static_assert(!RelationSelector<std::remove_cvref_t<decltype(fields::FSArticle.title)>>);

// Both resolve to their member for the NTTP positions.
static_assert(std::meta::identifier_of(selector_info<fields::FSArticle.tags>()) == "tags");

// ── A relation proxy carries NO comparison operators (#408 preserved) ────────
// This is the load-bearing difference from FieldRef, and the reason RelationRef
// does not derive from where::Field<M>. WHERE on a relation stays a compile error.
template <auto S> constexpr bool where_capable = requires { S == 1; };
static_assert(where_capable<fields::FSPerson.age>);    // a column: comparable
static_assert(!where_capable<fields::FSArticle.tags>); // a relation: NOT comparable

// ValidSelector gates the column positions, so it must REJECT a relation proxy.
static_assert(ValidSelector<fields::FSArticle.title>);
static_assert(!ValidSelector<fields::FSArticle.tags>);

// ── Field counts ─────────────────────────────────────────────────────────────
static_assert(storm::field_specs_for(^^FSPerson).size() == 3);
static_assert(storm::field_specs_for(^^FSArticle).size() == 3); // id, title, tags

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
