# Storm ORM — Python Bindings (Proof of Concept)

Thin Python wrapper over the Storm C++ ORM library using [nanobind](https://github.com/wjakob/nanobind).

## Build

```bash
cmake --preset ninja-python
cmake --build --preset ninja-python
```

The extension module is built at `build/python/python/_storm.cpython-3XX-...-linux-gnu.so`.

## Run the Demo

```bash
cd build/python/python
python3 demo.py
```

## API

```python
import _storm as storm

# Connect to SQLite database
storm.connect(":memory:")

# Create table for the Person model
storm.create_table()

# Insert
person = storm.Person(name="Alice", age=30)
person_id = storm.insert(person)       # returns auto-generated ID

storm.bulk_insert([
    storm.Person(name="Bob", age=25),
    storm.Person(name="Charlie", age=35),
])

# Select all
results = storm.select()               # list[Person]

# Filtered queries (WHERE) — Pythonic operator overloads
from _storm import Person
storm.select_where(Person.c_age > 30)
storm.select_where(Person.c_age == 25)
storm.select_where(Person.c_name == "Alice")
storm.select_where(Person.c_name.like("A%"))
# Column proxies: Person.c_id, Person.c_name, Person.c_age
# Supported ops: ==, !=, >, >=, <, <=, like (strings only)

# Count
total = storm.count()

# Remove
storm.remove(person)                    # by primary key
storm.remove_all()                      # delete all rows
```

## Architecture

The binding layer compiles as C++26 and uses `import storm;` to access the ORM.
A hardcoded `PyPerson` struct (id, name, age) is registered with Storm's reflection
system and exposed to Python via nanobind.

Key challenge solved: Clang's implicit module cache is keyed by compiler flags and
include paths. The storm static library and the Python extension module must have
identical flags/includes or the `import storm;` statement fails with module cache
conflicts. This is handled in `python/CMakeLists.txt`.

## Limitations (PoC scope)

- Single hardcoded model (`Person` with id/name/age)
- WHERE filters use runtime string dispatch over compile-time reflection branches
- SQLite only
- No `order_by`, `limit`, `offset`, `join`, or aggregate support exposed
- No transaction support

See issue #102 for the full Phase 2 roadmap.
