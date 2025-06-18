#pragma once
#include <refl.hpp> 

struct Post; // Forward declaration

struct Author {
    int id;
    std::string name;
    int age;
    std::string email;
    std::vector<std::shared_ptr<Post>> posts;
};

struct Post {
    int id;
    std::string title;
    std::string content;
    std::weak_ptr<Author> author;  // Weak pointer to avoid cycles
};

REFL_AUTO(
    type(Author),
    field(id),
    field(name),
    field(age),
    field(email)
)

REFL_AUTO(
    type(Post),
    field(id),
    field(title),
    field(content),
    field(author)
)

