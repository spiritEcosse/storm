# Referential Integrity (#412)

Storm enforces foreign-key relationships at the database level. Every `fk<>` column emits
a `REFERENCES` clause, every auto-generated many-to-many junction table carries
`FOREIGN KEY` constraints, and SQLite connections open with `PRAGMA foreign_keys = ON`.
This is **always on** — there is no opt-in annotation. The per-FK `ON DELETE` policy
(#431) is configurable via the `fk<RefAction::...>` template argument (see below).

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

An `fk<>` field generates an `<name>_id` column with a `REFERENCES` clause to the related
model's table (the FK field's C++ type, optional-unwrapped) and primary key `id`:

```cpp
struct Message {
    [[= storm::meta::FieldAttr::primary]] int    id{};
    std::string                                  content;
    [[= storm::meta::fk<>]] Person               sender;  // → sender_id
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

Auto-generated junction tables for `many_to_many<>` fields get a `FOREIGN KEY` on each
side, with `ON DELETE CASCADE` by default — an orphaned junction row is meaningless once
its owner or related entity is gone, so deleting the entity removes its link rows. The
default is overridable per field (see [ON DELETE policy](#on-delete-policy-431)):

```sql
CREATE TABLE Student_Course (
    Student_id INTEGER NOT NULL,
    Course_id INTEGER NOT NULL,
    PRIMARY KEY (Student_id, Course_id),
    FOREIGN KEY (Student_id) REFERENCES Student(id) ON DELETE CASCADE,
    FOREIGN KEY (Course_id) REFERENCES Course(id) ON DELETE CASCADE
);
```

## ON DELETE policy (#431)

The `ON DELETE` referential action is configurable per FK via the `fk<RefAction>`
template argument. `RefAction` is one of `Cascade`, `SetNull`, `Restrict`, `NoAction`.

| Spelling                              | Emitted clause            | Effect on the child row when the parent is deleted |
|---------------------------------------|---------------------------|----------------------------------------------------|
| `fk<>` / `fk<RefAction::Restrict>`    | *(none — SQL default)*    | delete is **rejected** while children exist        |
| `fk<RefAction::Cascade>`              | `ON DELETE CASCADE`       | child row is **deleted** too                       |
| `fk<RefAction::SetNull>`              | `ON DELETE SET NULL`      | child FK column is set to **NULL**                 |
| `fk<RefAction::NoAction>`             | `ON DELETE NO ACTION`     | like `RESTRICT` for immediate constraints          |

`RefAction::Restrict` is the default, so a bare `fk<>` emits a plain `REFERENCES` with no
`ON DELETE` clause — byte-identical to the pre-#431 DDL.

```cpp
struct Comment {
    [[= storm::meta::FieldAttr::primary]] int                          id{};
    [[= storm::meta::fk<storm::meta::RefAction::Cascade>]] Post         post;     // delete post → delete comments
    [[= storm::meta::fk<storm::meta::RefAction::SetNull>]] std::optional<User> author;  // delete user → author_id = NULL
};
```

**`SET NULL` requires a nullable FK** (`std::optional<Related>`) — it writes `NULL` into
the child column, which a `NOT NULL` column cannot hold. This is enforced at compile time
by the `ModelFkPoliciesValid<T>` constraint on `BaseStatement`: an `fk<RefAction::SetNull>`
on a non-optional field fails to compile with a clear constraint violation.

**`ON UPDATE` is not emitted.** Storm foreign keys reference synthetic identity primary
keys whose value never changes, so an `ON UPDATE` action would never fire. It can be added
later if natural-key FKs are introduced.

### Junction `ON DELETE` override

The junction default is `CASCADE`, overridable per m2m field via the
`many_to_many<RefAction>` template argument — applied to **both** junction FK sides. Use
it for soft-delete/archival, audit trails of links, or through-data you do not want to lose:

```cpp
struct Member {
    [[= storm::meta::FieldAttr::primary]] int id{};
    // both Member_id and Club_id sides emit ON DELETE RESTRICT instead of CASCADE
    [[= storm::meta::many_to_many<storm::meta::RefAction::Restrict>]] std::vector<Club> clubs;
};
```

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
- Deleting a parent that is still referenced by a `RESTRICT`/default FK. A `CASCADE` FK
  deletes the children; a `SetNull` FK nulls the child column instead.
- Leaving orphaned junction rows — deleting an owner/related entity cascades to its
  junction rows (unless the m2m field overrode the junction policy).

## Migration caveat

This is enforced on every connection. If you point Storm at an existing database that
already contains dangling foreign keys, enforcement will reject operations that touch
those rows (and, on SQLite, a `PRAGMA foreign_key_check` would report the existing
violations). Storm itself has no released schema, so this only matters when adopting
Storm against a pre-existing database.
