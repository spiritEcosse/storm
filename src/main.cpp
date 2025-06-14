#include "Person.h"
#include "QuerySet.h" // Defines orm::QuerySet<T>
#include "Connection.h"
#include <iostream>     // For std::cout
#include <vector>       // For std::vector
#include <memory>       // For std::make_shared

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

    orm::QuerySet<Person> persons(conn);

    // Print the generated CREATE TABLE statement
    std::cout << "\n=== SQL CREATE TABLE Statement (QuerySet<Person>) ===\n";
    persons.create_table();

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
