# Storm ORM

A lightweight C++ ORM (Object-Relational Mapping) library for SQLite, designed for simplicity and modern C++ development.

## Features
- Header-only ORM for C++
- Automatic table creation from C++ structs/classes
- Type-safe queries and inserts
- In-memory and file-based SQLite support

## Quick Start

### Prerequisites
- C++23 compiler with modules support
- [SQLite3](https://www.sqlite.org/index.html)

## Dependencies

Storm ORM requires the following libraries:

- **SQLite3**: For SQLite database support ([link](https://www.sqlite.org/index.html))
- **PostgreSQL**: For PostgreSQL database support ([link](https://www.postgresql.org/))
- **refl-cpp**: Header-only C++ reflection library ([link](https://github.com/veselink1/refl-cpp))

Optional (for testing):
- **GoogleTest**: C++ testing framework ([link](https://github.com/google/googletest))

All dependencies (except for SQLite3 and PostgreSQL client libraries) are automatically managed via [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) in the build system.

You must have the development files for SQLite3 and PostgreSQL installed on your system for building and linking.

### Build

```
mkdir -p build/debug
cd build/debug
cmake ../..
make
```

### Run Example

```
./storm
```

Sample output:
```
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

Opened database successfully: :memory:

=== SQL CREATE TABLE Statement (QuerySet<Person>) ===

=== SELECT ALL Persons ===
ID: 1, Age: 31, Salary: 50000, Married: Yes
ID: 2, Age: 40, Salary: 60000, Married: No
ID: 3, Age: 50, Salary: 70000, Married: Yes

=== SELECT ALL Persons ===
ID: 2, Age: 40, Salary: 60000, Married: No
ID: 3, Age: 50, Salary: 70000, Married: Yes
Closed database connection.
```

### Example Usage

```cpp
int main() {
    // Create a Connection to an in-memory SQLite database
    auto conn = std::make_shared<Connection>(":memory:");
    orm::QuerySet<Person> persons(conn);
    persons.create_table();
    Person p1{30, 50000.0, true};
    persons.insert(p1);
    std::vector<Person> personsToInsert;
    personsToInsert.emplace_back(40, 60000.0, false);
    personsToInsert.emplace_back(50, 70000.0, true);
    persons.insert(personsToInsert);
    // Remove a person
    persons.remove(p1);
    const auto all_persons = persons.select();
    for (const auto& p : all_persons) {
        std::cout << "ID: " << p.id << ", " 
                  << "Age: " << p.age 
                  << ", Salary: " << p.salary 
                  << ", Married: " << (p.is_married ? "Yes" : "No") << "\n";
    }
    return 0;
}
```

## Breaking Changes

- The `delete` method in `QuerySet` has been renamed to `remove` to avoid conflict with the C++ `delete` keyword. Update your code to use `remove` instead of `delete` when deleting objects from the database.


## Project Structure
- `src/` - Source code
- `main.cpp` - Example usage
- `QuerySet.h`, `Person.h`, `ReflectionUtils.h` - Core ORM headers

## Contributing
Contributions are welcome! Please open issues and submit pull requests.

## License

This project is licensed under the GNU Affero General Public License v3.0 (AGPL-3.0). See the [LICENSE](LICENSE) file for details.
