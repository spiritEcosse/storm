#include "Person.h"
#include "Post.h"
#include "Comment.h"
#include "QuerySet.h" // Defines orm::QuerySet<T>
#include "Connection.h"
#include <iostream>     // For std::cout
#include <vector>       // For std::vector
#include <memory>       // For std::make_shared
#include "SchemaManager.h"

// In your main application setup
void initialize_database(std::shared_ptr<Connection> conn) {
    SchemaManager schema_manager(conn);
    
    // Register all your models
    schema_manager.register_model<Person>();
    schema_manager.register_model<Post>();
    schema_manager.register_model<Comment>();
    
    // Create all tables at once
    schema_manager.create_all_tables();
}

void print_all(std::vector<Person> all_persons) {
    std::cout << "\n=== SELECT ALL Persons ===\n";
    for (const auto& p : all_persons) {
        std::cout << "ID: " << p.id << ", " 
            << "Age: " << p.age 
            << ", Salary: " << p.salary 
            << ", Married: " << (p.is_married ? "Yes" : "No") << "\n";
    }
}

int main() {
    // Create a Connection to an in-memory SQLite database
    auto conn = std::make_shared<Connection>(":memory:");
    initialize_database(conn);

    orm::QuerySet<Person> persons(conn);

    Person p1{30, 50000.0, true};
    persons.insert(p1);

    std::vector<Person> personsToInsert;
    personsToInsert.emplace_back(40, 60000.0, false);
    personsToInsert.emplace_back(50, 70000.0, true);
    persons.insert(personsToInsert);
    
    p1.age = 31;
    persons.update(p1);

    print_all(persons.select_all());
    persons.remove(p1);
    print_all(persons.select_all());
    return 0;
}
