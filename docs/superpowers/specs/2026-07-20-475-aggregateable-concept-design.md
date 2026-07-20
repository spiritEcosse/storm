# #475 — NumericAggregateable concept for aggregate functions

## Problem

Split from #206. `sum()`, `avg()`, `min()`, `max()` on `AggregateStatement`,
`GroupByBuilder`, and `QuerySet` currently accept **any** field type as their
aggregate target. The target field's type flows only into (a) the generated SQL
text via `std::meta::identifier_of` and (b) `op_has_floating_field` (a
floating-point check). The result type (`OpResult`) is always numeric —
`int64_t` / `double` / `std::optional<double>` — and extraction dispatches on
that *destination* type, never the source column. Consequently
`min<^^Person::name>()` (a `std::string` field) or `sum<^^Person::avatar>()`
(a BLOB) **compiles today and silently coerces** via SQLite, instead of failing
with a clear message.

## Premise verification (per issue instruction)

- Confirmed: no numeric aggregate meaningfully consumes a string/BLOB. `OpResult`
  for SUM/AVG/MIN/MAX is numeric in every branch.
- Confirmed: `COUNT` / `COUNT(DISTINCT)` legitimately count rows of **any** type
  and must stay unconstrained.
- Therefore the issue's illustrative `Aggregateable = arithmetic || string`
  concept has **no honest call site** and is dropped. Only
  `NumericAggregateable` is defined and applied. (User-confirmed.)

## Concept

In `src/orm/statements/aggregate.cppm`, `storm::orm::statements` namespace:

```cpp
template <typename T>
concept NumericAggregateable = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;
```

`bool` is excluded: it is arithmetic in C++, but SUM/AVG/MIN/MAX over a boolean
column is a modelling mistake, not a real query.

To constrain a variadic `std::meta::info... FieldInfos` pack, a fold helper:

```cpp
template <std::meta::info... FieldInfos>
concept AllNumericAggregateable =
    (NumericAggregateable<std::remove_cvref_t<typename [:std::meta::type_of(FieldInfos):]>> && ...);
```

An empty pack is vacuously true — but the numeric aggregate methods always take
at least one field in practice, and `count()` (which allows an empty pack for
`COUNT(*)`) is never constrained by this concept.

## Where applied

`requires AllNumericAggregateable<FieldInfos...>` on the four numeric aggregate
methods at all three call surfaces:

- `AggregateStatement::sum/avg/min/max` (chaining) — `aggregate.cppm`
- `GroupByBuilder::sum/avg/min/max` — `aggregate.cppm`
- `QuerySet::sum/avg/min/max` — `queryset.cppm`

`count`, `count_distinct` stay unconstrained (any field type is valid to count).

## Verification (compile-time only)

New TU `tests/query/test_aggregateable_concept.cpp`:

- Positive `static_assert(NumericAggregateable<int/int64_t/double/float/...>)`.
- Negative `static_assert(!NumericAggregateable<std::string>)`,
  `!NumericAggregateable<bool>`, `!NumericAggregateable<std::vector<uint8_t>>`,
  `!NumericAggregateable<Color>` (enum).
- Method-level rejection via the #472 variable-template wrapper trick so the
  constraint failure SFINAE-soft-fails instead of hard-erroring: assert that
  `qs.min<^^Person::name>()` etc. are **not** instantiable while
  `qs.min<^^Person::age>()` is.
- A `TEST(...) { SUCCEED(); }` body to register the TU with GTest.

Existing aggregate tests (all numeric fields) must still compile and pass —
they double as the positive integration check.

## Non-goals

- No runtime behavior change; numeric aggregates over numeric fields are
  byte-identical.
- `Aggregateable` (arithmetic||string) concept — dropped (no call site).
- No change to `count` / `count_distinct`.
