# Storm ORM Module Structure

## Source Tree

```
src/
├── storm.cppm                      # Main module with meta functionality
├── db/
│   ├── concept.cppm                # Database concepts
│   └── sqlite.cppm                 # SQLite implementation
└── orm/
    ├── queryset.cppm               # QuerySet ORM interface
    ├── utilities.cppm              # ConstexprString, SQLCache templates
    └── statements/
        ├── base.cppm               # BaseStatement utilities
        ├── insert.cppm             # InsertStatement
        ├── select.cppm             # SelectStatement (with JOIN support)
        ├── distinct.cppm           # DistinctStatement (DISTINCT queries)
        ├── update.cppm             # UpdateStatement
        ├── remove.cppm             # RemoveStatement
        └── join.cppm               # JoinStatement (SQL builder for FK JOINs)
```

## Module Dependencies

```
storm (main module)
├── storm_db_concept
├── storm_db_sqlite
├── storm_orm_statements_base
├── storm_orm_utilities
├── storm_orm_statements_{insert,update,remove,select,distinct,join}
└── storm_orm_queryset
```

## Module Naming Convention

- Uses underscores (`storm_db_sqlite`) due to compiler limitations
- Module names follow pattern: `storm_{category}_{component}`
- Examples:
  - `storm_db_concept` - Database abstraction layer
  - `storm_orm_queryset` - QuerySet ORM interface
  - `storm_orm_statements_insert` - INSERT statement implementation

## Circular Dependency Handling

- **Problem**: C++26 modules don't support circular dependencies
- **Solution**: Duplicate `FieldAttr` enum in both `storm` and `storm_orm_statements_base`
- **Alternative Considered**: Shared types module rejected due to increased complexity

## Thread Safety Considerations

1. **SQLite Level**: Thread-safe with `SQLITE_OPEN_FULLMUTEX`
2. **Connection Management**: NOT thread-safe - requires external synchronization
3. **SQL Caching**: Thread-local storage eliminates synchronization overhead
4. **Recommended**: Per-thread connections or external mutex

## Module Export Strategy

- All public APIs exported through main `storm` module
- Internal implementation details remain in sub-modules
- Users import only: `import storm;`
