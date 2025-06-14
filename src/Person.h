// Person.h
#pragma once
#include <string>
#include <refl.hpp> 

// The actual Person struct
struct Person {
    int age;
    double salary;
    bool is_married;
    int id = 0;
};

// Register the struct with refl-cpp
REFL_AUTO(
    type(Person),
    field(id),
    field(age),
    field(salary), 
    field(is_married)
)
