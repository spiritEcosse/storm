# Referential Integrity (#412)

Storm enforces foreign-key relationships at the database level. Every `FieldAttr::fk`
column emits a `REFERENCES` clause, every auto-generated many-to-many junction table
carries `FOREIGN KEY` constraints, and SQLite connections open with
`PRAGMA foreign_keys = ON`. This is **always on** — there is no opt-in annotation.

## Why always-on

Both backends recommend referential integrity:

- **PostgreSQL** enforces declared foreign keys natively; there is no switch.
- **SQLite** parses FK syntax always but only *enforces* it when the connection sets
  `PRAGMA foreign_keys = ON`. The default-off is a historical backward-compatibility
  choice; the SQLite docs recommend applications turn it on. Storm does so on every
  connection in `Connection::open()`.

Storm has no released schema to break, so enforcement is on by default rather than
opt-in. For an ORM, foreign-key enforcement is a defining feature.

## Base-table foreign keys

A `FieldAttr::fk` field generates an `<name>_id` column with a `REFERENCES` clause to
the related model's table (the FK field's C++ type, optional-unwrapped) and primary key
`id`:

```cpp
struct Message {
    [[= storm::meta::FieldAttr::primary]] int    id{};
    std::string                                  content;
    [[= storm::meta::FieldAttr::fk]] Person      sender;  // → sender_id
};
```

```sql
-- SQLite
CREATE TABLE Message (
    id INTEGER PRIMARY KEY,
    content TEXT NOT NULL,
    sender_id INTEGER NOT NULL REFERENCES Person(id)
);
-- PostgreSQL: sender_id BIGINT NOT NULL REFERENCES Person(id)
```

A nullable FK (`std::optional<Person>`) drops `NOT NULL` but keeps the `REFERENCES`:

```sql
owner_id INTEGER REFERENCES Person(id)
```

## Junction-table foreign keys

Auto-generated junction tables for `many_to_many` fields get a `FOREIGN KEY` on each
side, with `ON DELETE CASCADE` — an orphaned junction row is meaningless once its
owner or related entity is gone, so deleting the entity removes its link rows:

```sql
CREATE TABLE Student_Course (
    Student_id INTEGER NOT NULL,
    Course_id INTEGER NOT NULL,
    PRIMARY KEY (Student_id, Course_id),
    FOREIGN KEY (Student_id) REFERENCES Student(id) ON DELETE CASCADE,
    FOREIGN KEY (Course_id) REFERENCES Course(id) ON DELETE CASCADE
);
```

## ON DELETE / ON UPDATE policy

| Relationship   | Policy                          |
|----------------|---------------------------------|
| Base-table FK  | `RESTRICT` / `NO ACTION` (SQL default — deleting a referenced parent is rejected) |
| Junction FK    | `ON DELETE CASCADE`             |

A per-FK configurable `on_delete<CASCADE \| SET NULL \| RESTRICT>` annotation is a
planned follow-up ([#431](https://github.com/spiritEcosse/storm/issues/431)); today base
FKs use the safe `RESTRICT` default.

## Table-creation order

Because a referencing table cannot be created before its FK target exists (PostgreSQL
rejects a `REFERENCES` to a missing table; SQLite tolerated it),
`SchemaStatement<T>::create_table_if_not_exists(conn)` **creates the referenced parent
tables first**, recursively. The recursion is cycle-safe (self- and mutual-FK cycles are
broken by an internal set of already-created table names) and idempotent
(`CREATE TABLE IF NOT EXISTS`). Many-to-many related tables are likewise created before
their junction table.

```cpp
// Person does not need to be created first — create_table_if_not_exists<Message>
// creates Person, then Message.
storm::orm::schema::SchemaStatement<Message>::create_table_if_not_exists(conn);
```

## Behavior summary

With enforcement on, the database rejects:

- Inserting a child whose FK points at a non-existent parent.
- Deleting a parent that is still referenced (base FK, `RESTRICT`).
- Leaving orphaned junction rows — deleting an owner/related entity cascades to its
  junction rows.

## Migration caveat

This is enforced on every connection. If you point Storm at an existing database that
already contains dangling foreign keys, enforcement will reject operations that touch
those rows (and, on SQLite, a `PRAGMA foreign_key_check` would report the existing
violations). Storm itself has no released schema, so this only matters when adopting
Storm against a pre-existing database.
