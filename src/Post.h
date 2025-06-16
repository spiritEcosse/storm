#pragma once
#include <refl.hpp> 

struct Post {
    int id;
    std::string title;
    std::string content;
    int author_id;
};

REFL_AUTO(
    type(Post),
    field(id),
    field(title),
    field(content),
    field(author_id)
)

