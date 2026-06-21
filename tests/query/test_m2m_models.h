#ifndef TESTS_QUERY_TEST_M2M_MODELS_H
#define TESTS_QUERY_TEST_M2M_MODELS_H

// Many-to-many test models (#203). Include AFTER `import storm;` — the
// [[= storm::many_to_many*]] annotations need the storm module, and
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
    [[= storm::FieldAttr::primary]] int id{};
    std::string title;
};

// Phase 1: auto-generated junction table Student_Course (Student_id, Course_id).
struct Student {
    [[= storm::FieldAttr::primary]] int id{};
    std::string name;
    int age{};
    [[= storm::many_to_many<>]] std::vector<Course> courses;
};

// Phase 2: explicit junction model with metadata. ManyToMany<Enrollment> only
// names the through model, so a forward declaration is enough here.
struct Enrollment;

struct Pupil {
    [[= storm::FieldAttr::primary]] int id{};
    std::string name;
    int age{};
    [[= storm::many_to_many_through<Enrollment>]] std::vector<Course> courses;
};

struct Enrollment {
    [[= storm::FieldAttr::primary]] int id{};
    [[= storm::fk<>]] Pupil pupil;
    [[= storm::fk<>]] Course course;
    std::string grade;
};

// Container-coverage models (Phase 1 edge cases: plf::hive and shared_ptr elements).
struct Track {
    [[= storm::FieldAttr::primary]] int id{};
    std::string title;
};

struct Playlist {
    [[= storm::FieldAttr::primary]] int id{};
    std::string name;
    [[= storm::many_to_many<>]] plf::hive<Track> tracks;
};

struct Album {
    [[= storm::FieldAttr::primary]] int id{};
    std::string name;
    [[= storm::many_to_many<>]] std::vector<std::shared_ptr<Track>> tracks;
};

// Multi-relation model (#392): two auto-junction m2m fields on one owner.
struct Club {
    [[= storm::FieldAttr::primary]] int id{};
    std::string name;
};

struct Member {
    [[= storm::FieldAttr::primary]] int id{};
    std::string name;
    int age{};
    [[= storm::many_to_many<>]] std::vector<Course> courses;
    [[= storm::many_to_many<>]] std::vector<Club> clubs;
};

// Related model WITH an FK field (#392): the aggregate complete SQL must emit
// the related FK column as "<field>_id".
struct Topic {
    [[= storm::FieldAttr::primary]] int id{};
    std::string name;
};

struct Lesson {
    [[= storm::FieldAttr::primary]] int id{};
    std::string title;
    [[= storm::fk<>]] Topic topic;
};

struct Tutor {
    [[= storm::FieldAttr::primary]] int id{};
    std::string name;
    [[= storm::many_to_many<>]] std::vector<Lesson> lessons;
};

// Junction ON DELETE override (#431): many_to_many<RefAction::Restrict> on the m2m
// field flips BOTH junction FK sides from the CASCADE default to RESTRICT.
struct RestrictedClub {
    [[= storm::FieldAttr::primary]] int id{};
    std::string name;
};

struct RestrictedMember {
    [[= storm::FieldAttr::primary]] int id{};
    std::string name;
    [[= storm::many_to_many<storm::RefAction::Restrict>]] std::vector<RestrictedClub> clubs;
};

namespace storm::test {

// Seeds: Alice(20)→[Math, Physics], Bob(22)→[Math], Carol(25)→[] (no courses).
// Junction rows go through raw SQL — Phase 1 has no model for the auto junction.
template <typename ConnType> inline auto seed_students() -> void {
    storm::QuerySet<Student, ConnType> sqs;
    std::vector<Student> const students = {
        {.name = "Alice", .age = 20}, {.name = "Bob", .age = 22}, {.name = "Carol", .age = 25}};
    ASSERT_TRUE(sqs.insert(std::span<const Student>(students)).execute().has_value());

    storm::QuerySet<Course, ConnType> cqs;
    std::vector<Course> const courses = {{.title = "Math"}, {.title = "Physics"}};
    ASSERT_TRUE(cqs.insert(std::span<const Course>(courses)).execute().has_value());

    auto conn = storm::QuerySet<Student, ConnType>::get_default_connection();
    for (const auto *pair : {"(1, 1)", "(1, 2)", "(2, 1)"}) {
        ASSERT_TRUE(conn->execute(std::format("INSERT INTO Student_Course (Student_id, Course_id) VALUES {}", pair))
                        .has_value());
    }
}

// Seeds (#392 multi-relation): Ann→courses[Math,Physics] clubs[Chess],
// Ben→courses[Math] clubs[], Cat→courses[] clubs[Chess,Robotics], Dan→[] [].
template <typename ConnType> inline auto seed_members() -> void {
    storm::QuerySet<Member, ConnType> mqs;
    std::vector<Member> const members = {
        {.name = "Ann", .age = 20}, {.name = "Ben", .age = 22}, {.name = "Cat", .age = 25}, {.name = "Dan", .age = 30}};
    ASSERT_TRUE(mqs.insert(std::span<const Member>(members)).execute().has_value());

    storm::QuerySet<Course, ConnType> cqs;
    std::vector<Course> const courses = {{.title = "Math"}, {.title = "Physics"}};
    ASSERT_TRUE(cqs.insert(std::span<const Course>(courses)).execute().has_value());

    storm::QuerySet<Club, ConnType> kqs;
    std::vector<Club> const clubs = {{.name = "Chess"}, {.name = "Robotics"}};
    ASSERT_TRUE(kqs.insert(std::span<const Club>(clubs)).execute().has_value());

    auto conn = storm::QuerySet<Member, ConnType>::get_default_connection();
    for (const auto *pair : {"(1, 1)", "(1, 2)", "(2, 1)"}) {
        ASSERT_TRUE(
            conn->execute(std::format("INSERT INTO Member_Course (Member_id, Course_id) VALUES {}", pair)).has_value());
    }
    for (const auto *pair : {"(1, 1)", "(3, 1)", "(3, 2)"}) {
        ASSERT_TRUE(
            conn->execute(std::format("INSERT INTO Member_Club (Member_id, Club_id) VALUES {}", pair)).has_value());
    }
}

} // namespace storm::test

#endif // TESTS_QUERY_TEST_M2M_MODELS_H
