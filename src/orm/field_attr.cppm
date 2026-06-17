module;

#include <meta>

export module storm_orm_field_attr;

import std;

// Dependency-free leaf module (#387): single source of truth for the FieldAttr
// annotation enum, shared by statement modules and the public storm module.
// Same pattern as storm_db_concept — no Storm imports, so any module may use it
// without creating a cycle.
export namespace storm::meta {

    // Field annotation attributes read via C++26 reflection, e.g.
    //   [[= storm::meta::FieldAttr::primary]] int id;
    enum class FieldAttr : std::uint8_t {
        primary,
        primary_autoincrement,
        indexed,
        unique,
        fk,
        auto_create,
        auto_update,
        // 64-bit unsigned storage opt-ins (#436). A bare unsigned-64 field is a
        // compile-time error; it must carry exactly one of these:
        //   signed_storage — keep today's signed BIGINT/INTEGER (byte-identical, fast)
        //                     for values always <= INT64_MAX.
        //   full_unsigned  — order-preserving full-range storage: PG NUMERIC(20,0),
        //                     SQLite zero-padded 20-char TEXT (lexicographic == numeric).
        signed_storage,
        full_unsigned
    };

    // A field is "a primary key" for either annotation variant: plain `primary`
    // (plain INTEGER PRIMARY KEY) or `primary_autoincrement` (the SQLite never-reuse
    // opt-in, #379). Every PK-detection site routes through here so the two variants
    // can never drift apart.
    consteval auto is_primary_attr(FieldAttr attr) -> bool {
        using enum FieldAttr;
        return attr == primary || attr == primary_autoincrement;
    }

    // A 64-bit unsigned source type — the set that needs an explicit storage
    // annotation (#436). Signed-64 and all smaller types are unaffected.
    template <typename T>
    concept Unsigned64 = std::is_same_v<T, std::uint64_t> || std::is_same_v<T, unsigned long> ||
                         std::is_same_v<T, unsigned long long>;

    // True when the data member `member` has a 64-bit unsigned type (#436), looking
    // through std::optional<>. Reflection-level type comparison (not `if constexpr`),
    // so it is usable inside a consteval loop where the loop variable is not a core
    // constant expression. std::uint64_t is a typedef for one of the two canonical
    // unsigned types below (unsigned long on LP64), so comparing the dealiased member
    // type against both covers it — and `^^` can only reflect real types, not the
    // std::uint64_t using-declarator.
    consteval auto is_unsigned64_member(std::meta::info member) -> bool {
        std::meta::info t = std::meta::dealias(std::meta::type_of(member));
        if (std::meta::has_template_arguments(t) && std::meta::template_of(t) == ^^std::optional) {
            t = std::meta::dealias(std::meta::template_arguments_of(t)[0]);
        }
        return t == ^^unsigned long || t == ^^unsigned long long;
    }

    // True when `member` carries FieldAttr::full_unsigned (order-preserving storage).
    consteval auto has_full_unsigned_attr(std::meta::info member) -> bool {
        auto attr = std::meta::annotation_of_type<FieldAttr>(member);
        return attr.has_value() && attr.value() == FieldAttr::full_unsigned;
    }

    // True when `member` carries FieldAttr::signed_storage (legacy signed storage).
    consteval auto has_signed_storage_attr(std::meta::info member) -> bool {
        auto attr = std::meta::annotation_of_type<FieldAttr>(member);
        return attr.has_value() && attr.value() == FieldAttr::signed_storage;
    }

} // namespace storm::meta
