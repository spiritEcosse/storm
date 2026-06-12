module;

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
        auto_update
    };

    // A field is "a primary key" for either annotation variant: plain `primary`
    // (plain INTEGER PRIMARY KEY) or `primary_autoincrement` (the SQLite never-reuse
    // opt-in, #379). Every PK-detection site routes through here so the two variants
    // can never drift apart.
    consteval auto is_primary_attr(FieldAttr attr) -> bool {
        using enum FieldAttr;
        return attr == primary || attr == primary_autoincrement;
    }

} // namespace storm::meta
