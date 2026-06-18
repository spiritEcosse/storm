#pragma once
// Test model definitions for storm-schema — mirrors tests/test_models.h
// but without gtest dependency (avoids C++26 module conflicts).
// Models are in namespace `schema` for auto-discovery via reflection.
// import std; migration (issue #326): no std #includes here — std types come
// from the importing TU's `import storm; import std;`. A textual std #include in
// this header would clash with the std module already active in the consuming TU.

namespace schema {

struct Person {
    [[= storm::meta::FieldAttr::primary]] int id{};
    [[= storm::meta::FieldAttr::unique]] std::string name;
    int age{};
    double salary{};
    bool is_active{};
    int years_experience{};
    [[= storm::meta::FieldAttr::indexed]] std::string department;
    std::optional<int> score;
    std::optional<std::string> nickname;
    std::vector<std::uint8_t> avatar;
};

struct SimpleRecord {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
    int value{};
};

struct Message {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string content;
    int value{};
    [[= storm::meta::fk<>]] Person sender;
};

enum class Color : int { Red = 0, Green = 1, Blue = 2 };

struct ExtendedTypes {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::int64_t big_num{};
    double precise{};
    float approx{};
    unsigned int u_int{};
    long long ll_signed{};
    std::optional<double> opt_double;
    std::optional<std::int64_t> opt_int64;
    std::string label;
    signed char tiny_signed{};
    unsigned char tiny_unsigned{};
    char single_char{};
    Color color{Color::Red};
    std::chrono::year_month_day date_field{std::chrono::year{2000} / std::chrono::January / std::chrono::day{1}};
    std::chrono::system_clock::time_point datetime_field{};
    std::chrono::seconds duration_field{};
    std::filesystem::path file_path;
    std::vector<std::byte> raw_data;
    storm::UUID uuid_field;
    std::optional<Color> opt_color;
    std::optional<std::chrono::system_clock::time_point> opt_timestamp;
    std::optional<std::filesystem::path> opt_path;
};

struct Task {
    [[= storm::meta::FieldAttr::primary]] int id{};
    [[= storm::meta::fk<>]] Person assignee;
    [[= storm::meta::fk<>]] Person reviewer;
    std::string description;
};

} // namespace schema

// Composite indexes — must be outside namespace (specializing storm::Indexes)
template <> struct storm::Indexes<schema::Person> {
    using type = std::tuple<storm::Index<^^schema::Person::department, ^^schema::Person::age>,
                            storm::UniqueIndex<^^schema::Person::name, ^^schema::Person::department>>;
};
