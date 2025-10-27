# C++26 Module System

Storm ORM uses C++26 modules for better compilation times and encapsulation.

## Module Structure

```
src/
├── storm.cppm                      # Main module
├── db/
│   ├── concept.cppm                # Database concepts
│   └── sqlite.cppm                 # SQLite implementation
└── orm/
    ├── queryset.cppm               # QuerySet interface
    ├── utilities.cppm              # ConstexprString, SQLCache
    └── statements/
        ├── base.cppm               # BaseStatement utilities
        ├── insert.cppm, update.cppm, remove.cppm, select.cppm
        └── join.cppm               # JoinStatement
```

## Module Dependencies

```
storm (main) → orm → statements → base → utilities
            ↓
            db → concept + sqlite
```

## Module Naming

Uses underscores due to compiler limitations:
- `storm_db_concept`
- `storm_db_sqlite`
- `storm_orm_queryset`

## Avoiding Circular Dependencies

- BaseStatement provides shared utilities
- FieldAttr enum duplicated (can't import from main module)
- Concepts define interfaces without implementation details

## See Also

- [Compiler Issues](../development/COMPILER_ISSUES.md) - Module-related workarounds
