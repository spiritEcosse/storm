# C++26 Module System

Storm ORM uses C++26 modules for better compilation times and encapsulation.

**This is the canonical reference for the module structure.** Other documentation files reference this document.

## Module Structure

```
src/
├── storm.cppm                      # Main module - exports all public APIs
├── db/
│   ├── concept.cppm                # Database concepts (IConnection, IStatement)
│   └── sqlite.cppm                 # SQLite implementation
└── orm/
    ├── queryset.cppm               # QuerySet ORM interface
    ├── utilities.cppm              # ConstexprString, SQLCache templates
    ├── where.cppm                  # WHERE clause expressions and field() API
    └── statements/
        ├── base.cppm               # BaseStatement utilities (shared SQL generation)
        ├── insert.cppm             # InsertStatement (single + batch)
        ├── update.cppm             # UpdateStatement (single + batch)
        ├── remove.cppm             # RemoveStatement (single + batch)
        ├── select.cppm             # SelectStatement (with JOIN support)
        └── join.cppm               # JoinStatement (SQL builder, type-erased)
```

## Module Dependencies

```
storm (main module)
  ├─→ orm/queryset
  │     ├─→ orm/where
  │     └─→ orm/statements/select, insert, update, remove
  │           ├─→ orm/statements/join
  │           └─→ orm/statements/base
  │                 └─→ orm/utilities
  └─→ db/sqlite
        └─→ db/concept
```

**Key Dependency Rules:**
- `utilities.cppm` has no dependencies (base utility module)
- `base.cppm` only depends on `utilities.cppm`
- All statement modules depend on `base.cppm`
- `queryset.cppm` depends on all statement modules
- `where.cppm` is independent and imported by `queryset.cppm`

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
