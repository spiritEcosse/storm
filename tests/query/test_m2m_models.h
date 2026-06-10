#ifndef TESTS_QUERY_TEST_M2M_MODELS_H
#define TESTS_QUERY_TEST_M2M_MODELS_H

// Many-to-many test models (#203). Include AFTER `import storm;` — the
// [[= storm::meta::many_to_many*]] annotations need the storm module, and
// model structs stay textual because clang-p2996 loses annotations across
// BMI boundaries (#262).
//
// IMPORTANT: the including .cpp must `#include "plf_hive/plf_hive.h"` BEFORE
// `import std;` (fresh textual libc++ headers after the import clash with the
// std module — see COMPILER_ISSUES.md). The includes below are then no-ops.

#include <memory>
#include <plf_hive/plf_hive.h>
#include <string>
#include <vector>

struct Course {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string title;
};

// Phase 1: auto-generated junction table Student_Course (Student_id, Course_id).
struct Student {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
    int age{};
    [[= storm::meta::many_to_many]] std::vector<Course> courses;
};

// Phase 2: explicit junction model with metadata. ManyToMany<Enrollment> only
// names the through model, so a forward declaration is enough here.
struct Enrollment;

struct Pupil {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
    int age{};
    [[= storm::meta::many_to_many_through<Enrollment>]] std::vector<Course> courses;
};

struct Enrollment {
    [[= storm::meta::FieldAttr::primary]] int id{};
    [[= storm::meta::FieldAttr::fk]] Pupil pupil;
    [[= storm::meta::FieldAttr::fk]] Course course;
    std::string grade;
};

// Container-coverage models (Phase 1 edge cases: plf::hive and shared_ptr elements).
struct Track {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string title;
};

struct Playlist {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
    [[= storm::meta::many_to_many]] plf::hive<Track> tracks;
};

struct Album {
    [[= storm::meta::FieldAttr::primary]] int id{};
    std::string name;
    [[= storm::meta::many_to_many]] std::vector<std::shared_ptr<Track>> tracks;
};

#endif // TESTS_QUERY_TEST_M2M_MODELS_H
