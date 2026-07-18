# UPSERT Operations

Storm supports single-row `INSERT ... ON CONFLICT` through the insert proxy.
Choose an explicit unique conflict target, then select either `DO UPDATE` or
`DO NOTHING`. The target is checked at compile time: it must be a unique field
or exactly match a declared `UniqueIndex`. Primary keys are omitted from inserts
and therefore cannot be conflict targets.

## DO UPDATE

`update<Members...>()` overwrites the listed columns with the attempted
insert's values. It always returns the ID of the row that was inserted or
updated.

```cpp
auto result = qs.insert(user)
    .on_conflict<^^User::email>()
    .update<^^User::name, ^^User::age>()
    .execute();  // std::expected<int64_t, storm::db::Error>
```

```sql
INSERT INTO User (name, email, age) VALUES (?, ?, ?)
ON CONFLICT (email) DO UPDATE SET name=excluded.name, age=excluded.age
RETURNING id
```

Only the `update<...>()` members are overwritten. An `auto_update` timestamp is
also refreshed automatically. Foreign-key members use their database column
name (`owner_id`) in both the conflict target and update clause.

## DO NOTHING

`nothing()` inserts a row when the target is new and skips it when it already
exists.

```cpp
auto result = qs.insert(user)
    .on_conflict<^^User::email>()
    .nothing()
    .execute();  // std::expected<std::optional<int64_t>, storm::db::Error>
```

```sql
INSERT INTO User (name, email, age) VALUES (?, ?, ?)
ON CONFLICT (email) DO NOTHING
RETURNING id
```

| Operation | Result type | Conflict result |
| --- | --- | --- |
| `.update<...>()` | `std::expected<int64_t, Error>` | Existing row is updated; its ID is returned. |
| `.nothing()` | `std::expected<std::optional<int64_t>, Error>` | No row is changed; the value is `std::nullopt`. |

`DO NOTHING` is useful for get-or-create flows, but it does **not** return the
existing row's ID when a conflict is skipped. Use `DO UPDATE` when the ID is
needed on every call.

Bulk insert spans are not supported by the upsert chain.
