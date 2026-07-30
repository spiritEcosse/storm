#ifndef TESTS_QUERY_TEST_FIELDS_MODELS_H
#define TESTS_QUERY_TEST_FIELDS_MODELS_H

// Models local to the #518 fields-selector tests. Include AFTER `import storm;`
// — the [[= storm::*]] annotations need the module, and the fields:: blocks below
// call storm::field_specs_for, a function, which is a harder dependency still.

#include <meta>
#include <string>
#include <vector>

// A plain model: three scalar columns, no relations.
struct FSPerson {
    [[= storm::primary]] int id{};
    std::string name;
    int age{};
};

struct FSTag {
    [[= storm::primary]] int id{};
    std::string label;
};

// Carries a relation member — the generated struct MUST omit `tags`.
struct FSArticle {
    [[= storm::primary]] int id{};
    std::string title;
    [[= storm::many_to_many<>]] std::vector<FSTag> tags;
};

// Same as FSPerson plus one field — proves generation reads the model rather
// than a frozen list, with a byte-identical declaration below.
struct FSPersonExtra {
    [[= storm::primary]] int id{};
    std::string name;
    int age{};
    double salary{};
};

namespace fields {

// The 2-line user-side declaration this issue standardises. No field names, so
// it cannot drift when a model gains or loses a member.
struct FSPersonT;
consteval { std::meta::define_aggregate(^^FSPersonT, storm::field_specs_for(^^FSPerson)); }
inline constexpr FSPersonT FSPerson{};

struct FSArticleT;
consteval { std::meta::define_aggregate(^^FSArticleT, storm::field_specs_for(^^FSArticle)); }
inline constexpr FSArticleT FSArticle{};

struct FSPersonExtraT;
consteval { std::meta::define_aggregate(^^FSPersonExtraT, storm::field_specs_for(^^FSPersonExtra)); }
inline constexpr FSPersonExtraT FSPersonExtra{};

} // namespace fields

#endif // TESTS_QUERY_TEST_FIELDS_MODELS_H
