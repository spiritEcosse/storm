#pragma once

// IMPORTANT: Include this file AFTER `import storm;`
// The [[= storm::meta::FieldAttr::primary]] attribute requires the storm module
// to be imported before this struct is compiled.

#include <string>

struct FuzzModel {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
    int value{};
};

// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
inline constexpr const char *fuzz_model_create_sql = "CREATE TABLE IF NOT EXISTS FuzzModel ("
                                                     "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                                                     "name TEXT NOT NULL, "
                                                     "value INTEGER NOT NULL)";
// NOLINTEND(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)

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
    (void)conn->execute(fuzz_model_create_sql);
    if (with_seed) {
        (void)storm::QuerySet<FuzzModel>().insert(FuzzModel{.name = "seed", .value = 42}).execute();
    }
    return 0;
}

// NOLINTEND(modernize-use-trailing-return-type,bugprone-unused-return-value,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
