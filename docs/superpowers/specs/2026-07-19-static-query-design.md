# Static Query Path — Compile-Time SQL + Slot-Keyed Prepared Statements

**Issue**: [#462](https://github.com/spiritEcosse/storm/issues/462)
**Date**: 2026-07-19
**Status**: Approved design — implementation gated on the Phase 0 spike

## Problem

Storm generates shapeless SQL (table names, column lists, INSERT skeletons) at compile
time, but every query with a WHERE clause erases into a runtime `ExpressionVariant` tree
(`src/orm/where.cppm`): `f<^^Person::age>() > 30` starts from a compile-time
`CollatedField<MemberInfo>`, yet `operator>` immediately produces a runtime `Expr`. The
SQL string is assembled at runtime in `build_sql()` and amortized through the
string-keyed Connection statement cache (`prepare_cached`) — one hash plus string
compare on every execute, and a full SQL build on the first one.

For queries whose **shape is fully known at the call site** (fields, operators,
clause structure — everything except bound values), both costs are avoidable:

```
SQL text        → compile time (static constexpr, zero cost, forever)
prepare(stmt)   → runtime, once per connection (slot-indexed, amortized to ~zero)
bind + step     → runtime, every execution (irreducible)
```

The prepared statement itself can never be compile-time: `sqlite3_prepare_v2` compiles
SQL into VDBE bytecode against a live database (schema resolution, index selection) and
the statement is bound to a connection handle. The maximum achievable is a free lookup.

## Goal and gate

**Primary goal: hot-path performance.** The #214 cache investigation taught us that
"obviously faster" cache restructuring often shows zero measurable benefit, so the
entire feature is gated on a spike:

- **Gate: the slot-keyed static path must be ≥2% faster than today's
  `QuerySet.where().select()` steady-state hot path**, measured with interleaved runs,
  ≥10 repetitions, cv < 5%, Release build with verified `-O3` flags.
- If the gate fails, the issue is closed with the numbers and no production code is
  touched.

## Phase 0 — Spike

A throwaway three-way microbenchmark on a spike branch (never merged):

| Variant | SQL string | Statement lookup |
|---|---|---|
| Baseline | runtime `build_sql()` (today's QuerySet path) | string-keyed `prepare_cached` |
| B | `static constexpr` | string-keyed `prepare_cached` |
| A | `static constexpr` | slot index into per-Connection vector |

Workload: single-model `SELECT ... WHERE age > ?` execute+extract loop against a
populated table. Variant B isolates where any win comes from (SQL assembly vs statement
lookup). Numbers are posted on #462 before any further work.

## Design (post-gate)

### API surface — opt-in, additive

The existing `QuerySet` API is untouched; it **is** the dynamic path (runtime-composed
queries, reassignment, conditional chaining). There is no `.dynamic()` marker. The
static path is a new entry point whose chain steps each return a new type — runtime
composition is a type error by construction, not a silent downgrade:

```cpp
using storm::sf;   // static field — typed counterpart of storm::f<>

auto rows = storm::static_query<Person>()
    .where(sf<^^Person::age>() > min_age && sf<^^Person::name>() != excluded)
    .order_by<^^Person::name>()
    .limit(10)          // value is runtime — shape is "LIMIT ?", bound as a parameter
    .select();          // → std::expected<plf::hive<Person>, Error>
```

- `sf<^^M::field>()` returns `StaticField<Member>`. Its operators return **typed**
  expressions — `StaticCmp<Member, Op, V>`, `StaticBetween<Member, V>`,
  `StaticLike<Member>`, `StaticIsNull<Member, bool>`, `StaticAnd<L, R>`,
  `StaticOr<L, R>` — holding runtime values but keeping the shape in the type.
- Supported predicates (v1): all six comparisons (`==`, `!=`, `>`, `>=`, `<`, `<=`),
  `between()`, `like()`, `is_null()` / `is_not_null()`, arbitrary `&&` / `||` nesting.
- **No `in()`**: a runtime-sized value list changes the SQL text itself (number of
  placeholders), so it is inherently dynamic. Calling `in()` on a `StaticField` is a
  compile-time error (constraint violation) whose message points at the dynamic API.
- `limit()` / `offset()` take runtime values. Presence is encoded in the type
  (`LIMIT ?` / `OFFSET ?` appear in the constexpr SQL); the value is bound like any
  other parameter — no template instantiation per limit value.
- v1 executors: `select()` (all rows) and `to_sql()` (introspection / parity testing).
  `first()`, aggregates, UPDATE/DELETE, JOIN are follow-up issues only if v1 earns them.
- Backends: both SQLite (`?` placeholders) and PostgreSQL (`$1..$N`), dispatched on the
  connection type at compile time.

### Components

| Unit | Responsibility |
|---|---|
| `src/orm/static_where.cppm` | `sf<>`, typed expression types; consteval SQL-fragment rendering (type walk); in-order value binding (placeholder order = declaration order of the expression tree); reuses existing bind helpers |
| `src/orm/static_query.cppm` | `StaticQuery<Model, ConnType, WhereT, OrderBySpec, HasLimit, HasOffset>`; assembles the full SQL as `ConstexprString` at compile time, reusing `base.cppm` consteval table-name/field-list helpers |
| Slot registry (utilities) | `slot_of<QueryTag>()`: function-local `static` index initialized from a global atomic counter — one slot per static query type per process, thread-safe by C++ static-init rules |
| `Connection::prepare_slot(slot, sql_view)` (sqlite + postgresql) | Grow-on-demand vector of owned cached statements, array-indexed; prepared on first use, reset on reuse, destroyed with the connection |

### Statement lifetime

Ownership is identical to today's `prepare_cached`: the Connection owns its statements
and they die with it. Nothing lives outside the Connection — no `thread_local` statement
pointers, no generation counters (that alternative was rejected: it duplicates ownership
and contradicts the #214/#273 lessons). Per-thread connections remain the concurrency
model; the only cross-thread state is the slot counter (atomic).

### Error handling

Same `std::expected<_, Error>` contract as every existing execution path. Prepare, bind
and step failures propagate unchanged; no new error categories.

### Correctness invariant

For the same query shape, `static_query(...).to_sql()` must **byte-equal** the SQL the
dynamic path builds. This guarantees both paths hit identical prepared statements and
query plans, and pins the consteval renderer to the existing grammar.

## Testing (written first — rule 9)

TYPED_TEST on both backends (`DatabaseTypes`):

- All six comparison operators; `BETWEEN`, `LIKE`, `IS NULL` / `IS NOT NULL`;
  nested `(A && B) || C`.
- `order_by` / `limit` / `offset` in isolation and combined with WHERE.
- Empty, single-row and 100+ row result sets; int / string / double coverage.
- Repeated execution of one static query (slot-cache reuse); two different static
  queries plus a dynamic query interleaved on one connection.
- SQL-parity test against the dynamic path (byte equality).
- Mock error tests for the prepare-failure path (test_orm_mock_errors.cpp pattern).
- 100% coverage on new code; SonarCloud gate clean.

## Delivery plan

1. **Spike** on a throwaway branch → numbers posted on #462 → go/no-go against the 2% gate.
2. If go: tests-first implementation on `feature/462-static-query-path`.
3. Full benchmark suite re-run — revert if ANY slowdown elsewhere (rule 6).
4. Docs: `docs/guide/features/STATIC_QUERIES.md` (new), CLAUDE.md, affected
   `.claude/agents/*.md` — committed with the code (rule 8).
5. PR → SonarCloud gate → CI (debug, asan-ubsan, tsan) → squash-merge → close #462.

## Rejected alternatives

- **Static-by-default with `.dynamic()` escape hatch** — conceptually clean, but every
  chain step returning a new type breaks the documented reassignment pattern
  (`auto young = base.where(...)`) and therefore every existing call site, test and doc.
  Opt-in keeps the change additive.
- **`thread_local` per-type statement cache with generation check** — fastest lookup on
  paper, but invents an invalidation protocol and moves statement ownership outside the
  Connection (rejected per #214/#273 history).
- **Consteval SQL feeding `prepare_cached` only (variant B) as the end state** — kept as
  a spike rung to attribute the win, but it retains the per-execute hash+compare and is
  expected to fall under the gate.
