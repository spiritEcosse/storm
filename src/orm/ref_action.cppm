module;

export module storm_orm_ref_action;

import std;

// Dependency-free leaf module (#562): RefAction plus its SQL-phrase lookup, split out
// of storm_orm_field_attr so a consumer that only needs the FK/junction ON DELETE
// policy (e.g. storm_orm_relation_meta) is not invalidated by churn in the much larger
// field_attr leaf (#492, #500, #504, #509, #493 all touch it). Re-exported from
// storm_orm_field_attr so the storm::meta::RefAction / storm::RefAction spellings are
// unchanged for every existing caller.
export namespace storm::meta {

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

} // namespace storm::meta
