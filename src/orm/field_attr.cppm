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
    // NOTE: foreign keys are NOT a FieldAttr enumerator — they use the class-template
    // annotation storm::meta::fk<RefAction> (see statements::meta::Fk in base.cppm),
    // because the FK carries an optional ON DELETE policy (#431) and an enum member
    // cannot be templated. Bare FK is spelled [[= storm::meta::fk<>]].
    enum class FieldAttr : std::uint8_t {
        primary,
        primary_autoincrement,
        indexed,
        unique,
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

    // Referential action for a foreign key's ON DELETE policy (#431). Carried as the
    // template argument of the FK annotation `fk<RefAction>` (and of `many_to_many<RefAction>`
    // for the auto-junction). `Restrict` is the SQL default and Storm's pre-#431 behaviour:
    // a bare `fk<>` emits no ON DELETE clause at all, so existing DDL is byte-identical.
    enum class RefAction : std::uint8_t {
        Cascade,  // delete the child rows when the parent row is deleted
        SetNull,  // set the child FK column to NULL (requires a nullable FK)
        Restrict, // block the parent delete while children exist (SQL default)
        NoAction, // defer the check; like Restrict for immediate constraints
    };

    // The SQL keyword phrase for a RefAction, e.g. RefAction::SetNull → "SET NULL".
    // Longest phrase ("NO ACTION") is 9 chars; " ON DELETE NO ACTION" is 20.
    consteval auto ref_action_sql(RefAction action) -> std::string_view {
        using enum RefAction;
        switch (action) {
        case Cascade:
            return "CASCADE";
        case SetNull:
            return "SET NULL";
        case Restrict:
            return "RESTRICT";
        case NoAction:
            return "NO ACTION";
        }
        return {};
    }

    // A field is "a primary key" for either annotation variant: plain `primary`
    // (plain INTEGER PRIMARY KEY) or `primary_autoincrement` (the SQLite never-reuse
    // opt-in, #379). Every PK-detection site routes through here so the two variants
    // can never drift apart.
    consteval auto is_primary_attr(FieldAttr attr) -> bool {
        using enum FieldAttr;
        return attr == primary || attr == primary_autoincrement;
    }

    // Per-attribute field predicates (#421): the single source of truth for the
    // `annotation_of_type<FieldAttr>(member) == FieldAttr::X` idiom, so the same
    // test cannot drift between statement modules. FK detection lives in is_fk_field
    // below (an fk<...> class-template annotation, not a FieldAttr enumerator).
    consteval auto is_unique(std::meta::info member) -> bool {
        auto attr = std::meta::annotation_of_type<FieldAttr>(member);
        return attr.has_value() && attr.value() == FieldAttr::unique;
    }

    consteval auto is_indexed(std::meta::info member) -> bool {
        auto attr = std::meta::annotation_of_type<FieldAttr>(member);
        return attr.has_value() && attr.value() == FieldAttr::indexed;
    }

    // auto_create stamps now() on INSERT only; auto_update on both INSERT and UPDATE (#209).
    consteval auto is_auto_create(std::meta::info member) -> bool {
        auto attr = std::meta::annotation_of_type<FieldAttr>(member);
        return attr.has_value() && attr.value() == FieldAttr::auto_create;
    }

    consteval auto is_auto_update(std::meta::info member) -> bool {
        auto attr = std::meta::annotation_of_type<FieldAttr>(member);
        return attr.has_value() && attr.value() == FieldAttr::auto_update;
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

    // Foreign-key annotation (#431). A class-template annotation carries the optional
    // ON DELETE policy (RefAction); a FieldAttr enumerator cannot be templated, and an FK
    // needs the extra parameter. It lives in this leaf module so every statement module
    // (schema, join, field_names, distinct, base) can detect FK fields without importing
    // each other.
    //   [[= storm::meta::fk<>]] Person sender;                       // bare FK (RESTRICT)
    //   [[= storm::meta::fk<RefAction::Cascade>]] Person owner;      // ON DELETE CASCADE
    //   [[= storm::meta::fk<RefAction::SetNull>]] std::optional<Person> owner;  // SET NULL
    // The default RefAction::Restrict matches the SQL default, so bare fk<> emits a plain
    // REFERENCES with no ON DELETE clause (byte-identical to pre-#431 DDL).
    template <RefAction Action = RefAction::Restrict> struct Fk {
        static constexpr RefAction on_delete = Action;
    };
    // NOLINTNEXTLINE(readability-identifier-length) — `fk` is the public annotation spelling
    template <RefAction Action = RefAction::Restrict> inline constexpr Fk<Action> fk{};

    // Reflection of the Fk<...> annotation TYPE carried by `member`, if any.
    consteval auto fk_annotation_type_of(std::meta::info member) -> std::optional<std::meta::info> {
        for (const auto annotation : std::meta::annotations_of(member)) {
            const auto type = std::meta::type_of(annotation);
            if (std::meta::has_template_arguments(type) && std::meta::template_of(type) == ^^Fk) {
                return type;
            }
        }
        return std::nullopt;
    }

    // True when `member` carries an fk<...> annotation (#431) — the FK-field predicate.
    consteval auto is_fk_field(std::meta::info member) -> bool {
        return fk_annotation_type_of(member).has_value();
    }

    // The ON DELETE RefAction of an fk<...> FK, or std::nullopt when the field is not an FK
    // or carries the default RESTRICT (caller emits no clause then, keeping the
    // plain-REFERENCES DDL byte-identical).
    consteval auto fk_on_delete_action_of(std::meta::info member) -> std::optional<RefAction> {
        const auto type = fk_annotation_type_of(member);
        if (!type.has_value()) {
            return std::nullopt;
        }
        const auto action = std::meta::extract<RefAction>(std::meta::template_arguments_of(type.value())[0]);
        return action == RefAction::Restrict ? std::nullopt : std::optional<RefAction>(action);
    }

} // namespace storm::meta
