#pragma once

// IMPORTANT: Include this file AFTER `import storm;` and `import std;`.
// The [[= storm::FieldAttr::primary]] attribute requires the storm module
// to be imported before this struct is compiled. std::string is supplied by the
// preceding `import std;` — a textual <string> here would be re-pulled after the
// module and trip a redeclaration error (COMPILER_ISSUES §9, Finding B).

struct FuzzModel {
    [[= storm::FieldAttr::primary]] int id{};
    std::string name;
    int value{};
};

// NOLINTBEGIN(modernize-use-trailing-return-type,bugprone-unused-return-value,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

/// Common DB initialisation — call from LLVMFuzzerInitialize().
/// @param with_seed  Insert one seed row so WHERE/LIKE queries can match.
/// @return 0 (libFuzzer convention; failures are non-fatal here).
inline int fuzz_init_db(bool with_seed = true) {
    auto result = storm::QuerySet<FuzzModel>::set_default_connection(":memory:");
    if (!result.has_value()) {
        return 0;
    }
    const auto &conn = storm::QuerySet<FuzzModel>::get_default_connection();
    (void)storm::orm::schema::SchemaStatement<FuzzModel>::create_table_if_not_exists(conn);
    if (with_seed) {
        (void)storm::QuerySet<FuzzModel>().insert(FuzzModel{.name = "seed", .value = 42}).execute();
    }
    return 0;
}

// NOLINTEND(modernize-use-trailing-return-type,bugprone-unused-return-value,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
