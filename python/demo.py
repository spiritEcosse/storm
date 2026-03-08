#!/usr/bin/env python3
"""Storm ORM — Python bindings demo (proof of concept).

Demonstrates a full CRUD cycle using the nanobind-wrapped Storm C++ ORM.
"""

import _storm as storm
from _storm import Person

DB_PATH = ":memory:"


def main():
    # ── Connect ──────────────────────────────────────────────────────
    storm.connect(DB_PATH)
    print(f"Connected to SQLite database: {DB_PATH}")

    # ── Create table ─────────────────────────────────────────────────
    storm.create_table()
    print("Created 'pyperson' table")

    # ── Insert single records ────────────────────────────────────────
    alice = Person(name="Alice", age=30)
    alice_id = storm.insert(alice)
    print(f"Inserted: {alice} (id={alice_id})")

    bob = Person(name="Bob", age=25)
    storm.insert(bob)
    print(f"Inserted: {bob}")

    # ── Bulk insert ──────────────────────────────────────────────────
    people = [
        Person(name="Charlie", age=35),
        Person(name="Diana", age=28),
        Person(name="Eve", age=42),
    ]
    storm.bulk_insert(people)
    print(f"Bulk inserted {len(people)} people")

    # ── Select all ───────────────────────────────────────────────────
    print("\n--- All records ---")
    for person in storm.select():
        print(f"  {person}")

    # ── Count ────────────────────────────────────────────────────────
    total = storm.count()
    print(f"\nTotal records: {total}")

    # ── Filtered queries (WHERE) — Pythonic operator overloads ───────
    print("\n--- WHERE age > 30 ---")
    for person in storm.select_where(Person.c_age > 30):
        print(f"  {person}")

    print("\n--- WHERE age == 25 ---")
    for person in storm.select_where(Person.c_age == 25):
        print(f"  {person}")

    print("\n--- WHERE name == 'Alice' ---")
    for person in storm.select_where(Person.c_name == "Alice"):
        print(f"  {person}")

    print("\n--- WHERE name LIKE 'C%' ---")
    for person in storm.select_where(Person.c_name.like("C%")):
        print(f"  {person}")

    # ── Remove single record ─────────────────────────────────────────
    storm.remove(bob)
    print(f"\nRemoved: Bob")
    print(f"Records after remove: {storm.count()}")

    # ── Remove all ───────────────────────────────────────────────────
    storm.remove_all()
    print(f"Records after remove_all: {storm.count()}")

    print("\nDemo complete!")


if __name__ == "__main__":
    main()
