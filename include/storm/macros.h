#pragma once

// Storm ORM Macros
// Include this header to get access to Storm's convenience macros
// These cannot be exported through C++26 modules due to language limitations

// Macro helper for cleaner syntax: field(&Class::member) > value
// We need this as a macro because we need compile-time member pointer constants
#define field(member_ptr) ::storm::Field<member_ptr>{}
