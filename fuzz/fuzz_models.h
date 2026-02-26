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
