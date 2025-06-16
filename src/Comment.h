#pragma once
#include <refl.hpp> 

struct Comment {
    int id;
    std::string content;
    int post_id;
};

REFL_AUTO(
    type(Comment),
    field(id),
    field(content),
    field(post_id)
)
