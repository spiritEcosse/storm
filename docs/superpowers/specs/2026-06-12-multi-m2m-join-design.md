# Multiple m2m joins in one query — `join<m2m_a, m2m_b>()` (#392)

Date: 2026-06-12
Issue: #392 (enabler: #391 two-query predicate-pushdown m2m execution)

## Goal

Allow eager-loading **multiple** many-to-many relations of the same model in one
call:

```cpp
member_qs.join<^^Member::courses, ^^Member::clubs>().select();
member_qs.left_join<^^Member::courses, ^^Member::clubs>().select();
```

Under #391 each relation is one additional Q2 keyed off the same Q1 base set —
cost is additive (`K1 + K2` rows), never multiplicative.

## Execution model

- **Q1** (shared): `SELECT <base cols> FROM <Base> [WHERE][ORDER BY][LIMIT/OFFSET]`
  — runs **once**; builds the `pk → entity` hash map once. Q1 text depends only
  on the base model + clauses, so it is identical for every relation.
- **Q2 per relation**: `SELECT t2.<owner>_id, t3.<related cols> FROM <junction_i>
  t2 INNER JOIN <Related_i> t3 … WHERE t2.<owner>_id IN (<the same base
  subquery>)` — stitched into that relation's container via the shared map.
- All queries run inside the existing single transaction
  (`execute_m2m_2query`).

## Semantics

- One call applies **one join type to all listed relations** (`join` = INNER,
  `left_join` = LEFT). `is_left` is still stored per relation descriptor so a
  future chaining-merge API needs no wrapper change.
- **INNER**: after the stitch, drop an entity if its container is empty in
  **any** INNER relation (the SQL analog — a join over an empty set is empty).
- **LEFT** (Django semantics, per the issue): keep every base entity; each
  relation's container is independently empty/filled.
- WHERE/ORDER BY/LIMIT/OFFSET bound the **base** entity set, exactly as for a
  single relation (they live in Q1 and inside every Q2's IN-subquery).
- `first()`/`get()`/`rows()` work unchanged — they already route through
  `execute_m2m_2query`.
- Chaining a second `join()` call still **overwrites** the previous join
  (existing QuerySet semantics, unchanged).

## Out of scope (per issue)

- Mixed FK-join + m2m in one call — the `requires` clause keeps rejecting it.
- Mixed INNER/LEFT in one call — not expressible by the API shape; covered by
  per-descriptor `is_left` for a future issue.

## Component changes

### `join.cppm`

1. New exported per-relation descriptor:

   ```cpp
   struct M2MRelation {
       JoinStatementWrapper::ClauseSqlFn build_q2_sql_fn;
       auto (*extract_q2_owner_pk_fn)(ErasedStatementPtr) -> std::int64_t;
       auto (*append_related_q2_fn)(ErasedStatementPtr, ErasedObjectPtr) -> void;
       auto (*container_empty_fn)(ErasedObjectPtr) -> bool;
       bool is_left;
   };
   ```

2. `JoinStatementWrapper` replaces the five single-relation m2m fn-pointers +
   `m2m_is_left` with `std::vector<M2MRelation> m2m_relations;` (empty for FK
   joins — no allocation). `build_q1_sql_fn` stays wrapper-level (shared Q1).
   `is_m2m()` ⇒ `!m2m_relations.empty()`. The wrapper stops being trivially
   copyable; all existing uses copy it, which remains valid.

3. `M2MJoinStatement<T, Conn, Type, M2MField>` stays **single-field**. Its
   monolithic `select_prefix_arr_`/`join_suffix_arr_` complete-SQL fragments are
   split so a multi-relation complete SQL can be assembled with unique aliases:
   - consteval `base_select_arr_` — `"SELECT t1.<c1>, t1.<c2>…"` (base columns
     only) and `base_from_arr_` — `" FROM (SELECT <cols> FROM <Base>) t1"`.
   - runtime appenders on the single instantiation (alias as runtime arg,
     names from constexpr storage):
     `append_complete_cols(std::string&, std::size_t related_alias)` and
     `append_complete_join(std::string&, std::size_t junction_alias)`.

4. `make_m2m_join_wrapper<T, Conn, Type, Fields...>` becomes variadic:
   - fills one `M2MRelation` per field (fold);
   - `get_complete_sql_fn` assembles the chained join once
     (function-local `static const std::string`): relation *i* uses junction
     alias `2+2i`, related alias `3+2i` — relation 0 keeps `t2`/`t3`, so the
     single-relation SQL text is byte-identical to today's.

### `select.cppm`

- `execute_m2m_2query`: Q1 + map unchanged; then
  `for (rel : wrapper.m2m_relations) run_q2_stitch(rel, …)`; the INNER drop
  pass erases an entity when any `!rel.is_left` relation reports
  `container_empty_fn`.
- `run_q2_stitch` / `drop_empty_relations` take `const M2MRelation&` (drop pass
  iterates the relation vector).
- `build_sql` (the `.sql()` debug surface): `Q1 + "; " + Q2a + "; " + Q2b + …`.

### `queryset.cppm`

- `join()` / `left_join()` requires-clauses: replace
  `sizeof...(FKFields) == 1 && (M2MFieldOf && ...)` with
  `(M2MFieldOf && ...) && m2m_fields_distinct<FKFields...>()`; the m2m arm
  passes the whole pack to `make_m2m_join_wrapper`.
- New consteval `m2m_fields_distinct` (identifier pairwise-distinct) — a
  duplicated field would silently double-fill one container.
- `right_join()` unchanged (FK-only).

### Aggregates / DISTINCT / set-ops over multi-m2m

`get_complete_sql()` consumers (aggregate.cppm, distinct.cppm,
`capture_operand`) receive the chained N-junction join — `COUNT(*)` counts
cartesian tuples, the consistent extension of the existing single-relation
"(base, related) pairs" semantics. Documented, covered by one test.

## Test plan (fail-first, `tests/query/test_many_to_many_multi.cpp`)

New models in `test_m2m_models.h` (existing models untouched):
`Club { id, name }`, `Member { id, name, age, courses (auto m2m → Course),
clubs (auto m2m → Club) }`.

- Compile-time: multi-m2m `join`/`left_join` accepted; FK+m2m mix and duplicate
  m2m fields still rejected (`requires`-based negative `static_assert`s on
  well-formedness).
- `.sql()` shape: three `; `-separated statements; Q2a/Q2b each filtered by the
  identical base subquery.
- INNER `join<courses, clubs>`: only members non-empty in **both** relations.
- Entity empty in exactly one relation: dropped by INNER, kept by LEFT with
  that container empty.
- LEFT: all members; per-relation containers independently empty/filled.
- WHERE + ORDER BY + LIMIT bound the shared base set; both relations stitch
  onto the same bounded set.
- `first()` / `get()` with two relations.
- `rows()` generator with two relations.
- `count()` over multi-m2m = cartesian tuple count.
- Statement-cache correctness: repeated identical multi-m2m query; then a
  different (single-relation) query on the same QuerySet.
- Error paths (mock, `test_orm_mock_errors.cpp` pattern): failure in the
  second Q2 propagates and rolls back.
- All runtime tests TYPED_TEST on SQLite + PostgreSQL.

Existing single-relation m2m tests must pass unchanged (text-identical SQL).

## Verification gates

Release benchmarks (no regression on FK-join/SELECT hot paths — the wrapper
gains a vector member), ninja-asan-ubsan, ninja-tsan, coverage, SonarCloud
strict gate, docs (`JOIN_OPERATIONS.md`, CLAUDE.md m2m note).
