module;

#include <meta>

export module storm_orm_fields;

import std;

import storm_orm_field_attr;    // ValidFieldInfo
import storm_orm_relation_meta; // is_relation_field
import storm_orm_where;         // Field<M> — the proxy's base class

// Field-selector proxies (#518). NOT a leaf module: FieldRef derives from
// where::Field<M>, so this sits ABOVE storm_orm_where in the layering
//   field_attr, relation_meta -> where -> fields -> queryset/aggregate/orderby/insert
// Never make where.cppm import this module — that is a cycle, and it is why f<>
// stays in where.cppm unwidened (it cannot call selector_info from below).
export namespace storm::meta {

    // The public field-selector proxy.
    //
    // Deriving from Field<M> is what makes the bare form `fields::Person.age == 30`
    // work: Field<M> is STATELESS — every operator reads field_name_sv, a static
    // constexpr off the template parameter — so inheritance donates all twelve
    // comparison/in/like/between/is_null/collate operators with no duplication and
    // no per-instance data. Mutation-verified: removing the base breaks member
    // access on the proxy.
    //
    // The member reflection also lives in the TYPE (via M), which is what lets
    // `fields::Person.age` be used as a template argument for the NTTP positions.
    template <std::meta::info M>
        requires(ValidFieldInfo<M> && !is_relation_field(M))
    struct FieldRef : storm::orm::where::Field<M> {
        static constexpr std::meta::info member = M;
    };

    namespace detail {
        template <typename S> constexpr bool        is_field_ref              = false;
        template <std::meta::info M> constexpr bool is_field_ref<FieldRef<M>> = true;
    } // namespace detail

    // True iff S is a generated field proxy (i.e. some FieldRef<M>).
    template <typename S>
    concept FieldSelector = detail::is_field_ref<std::remove_cvref_t<S>>;

    // Object -> info converter for the NTTP selector positions (order_by, distinct,
    // values, group_by, the aggregates, join, update, upsert). Deliberately a free
    // function template rather than a member (`fields::Person.age.to_info()`): a
    // member would leak the internal representation into the public surface, and
    // std::meta::info is consteval-only so it cannot be returned into a runtime
    // expression.
    //
    // The raw-info branch is transitional scaffolding for one specific window: once
    // the QuerySet/aggregate/order_by methods are constrained on ValidSelector, the
    // not-yet-migrated ^^Model::member call sites still have to compile. It is what
    // lets the widening land WITHOUT the call-site migration in the same commit.
    // Once every site is migrated and f<> is gone, drop the info branch and this
    // collapses to `decltype(S)::member`.
    template <auto S> consteval auto selector_info() -> std::meta::info {
        if constexpr (std::same_as<std::remove_cvref_t<decltype(S)>, std::meta::info>) {
            return S;
        } else {
            return std::remove_cvref_t<decltype(S)>::member;
        }
    }

    // The widened public gate: accepts either spelling, validating whichever it got.
    // Both arms read through remove_cvref_t: FieldSelector already applies it
    // internally, so spelling it on only one arm would leave the reader checking
    // whether the asymmetry is load-bearing. It is not.
    template <auto S>
    concept ValidSelector = (FieldSelector<std::remove_cvref_t<decltype(S)>> ||
                             std::same_as<std::remove_cvref_t<decltype(S)>, std::meta::info>) &&
                            ValidFieldInfo<selector_info<S>()> && !is_relation_field(selector_info<S>());

    // The define_aggregate spec list for `owner`: one FieldRef-typed member per
    // persisted column, named after the model's member. Relation members (m2m,
    // reverse_fk) are skipped, mirroring the f<>() constraint — without the filter
    // `fields::Article.tags` would offer a selector for a non-column that fails deep
    // inside SQL generation.
    consteval auto field_specs_for(std::meta::info owner) -> std::vector<std::meta::info> {
        std::vector<std::meta::info> specs;
        for (const std::meta::info member :
             std::meta::nonstatic_data_members_of(owner, std::meta::access_context::unchecked())) {
            if (is_relation_field(member)) {
                continue;
            }
            specs.push_back(
                    std::meta::data_member_spec(
                            std::meta::substitute(^^FieldRef, {std::meta::reflect_constant(member)}),
                            {.name = std::meta::identifier_of(member)}
                    )
            );
        }
        return specs;
    }

} // namespace storm::meta
