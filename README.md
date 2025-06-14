# Storm ORM

A lightweight C++ ORM (Object-Relational Mapping) library for SQLite, designed for simplicity and modern C++ development.

## Features
- Header-only ORM for C++
- Automatic table creation from C++ structs/classes
- Type-safe queries and inserts
- In-memory and file-based SQLite support

## Quick Start

### Prerequisites
- C++17 or newer compiler
- [SQLite3](https://www.sqlite.org/index.html)

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
Opened database successfully: :memory:

=== SQL CREATE TABLE Statement (QuerySet<Person>) ===

=== SELECT ALL Persons ===
ID: 1, Age: 30, Salary: 50000, Married: Yes
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

## Project Structure
- `src/` - Source code
- `main.cpp` - Example usage
- `QuerySet.h`, `Person.h`, `ReflectionUtils.h` - Core ORM headers

## Contributing
Contributions are welcome! Please open issues and submit pull requests.

## License
[MIT License](LICENSE)
