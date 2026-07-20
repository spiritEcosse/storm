# ValidForeignKey concept for JOIN/FK validation (#474)

## Problem

JOIN methods (`join<^^T::field>()`, `left_join`, …) and FK extraction currently validate
FK targets implicitly. Two facts drove the design after auditing the tree:

1. `find_fk_primary_key<FKType>()` (`src/orm/statements/base.cppm:466`) is **already**
   constrained by an *inline* `requires ModelWithPrimaryKey<utilities::optional_inner_type_t<FKType>>`
   — the exact body the issue proposes, but unnamed and one-off.
2. `FKFieldOf<T, Member>` (`base.cppm:259`) — the concept that gates every `join<>` /
   `left_join<>` entry point — checks only that the selected member **is** an FK
   (`meta::is_fk_field`). It does **not** check that the FK's referenced entity has a
   primary key. A PK-less FK target therefore fails **deep inside extraction** (when
   `find_fk_primary_key` is instantiated), not at the `join<>` call site.

So a named `ValidForeignKey` concept is **not** pure duplication of `ModelFkPoliciesValid`
(that concept only enforces the `ON DELETE SetNull` → nullable-FK rule, unrelated to PK
existence). It (a) single-sources the inline `requires` on `find_fk_primary_key`, and
(b) closes the real gap in `FKFieldOf` by moving the "FK target has no primary key" error
to the call site.

## Design

### The concept (single parameter)

Added to `src/orm/statements/base.cppm` in `storm::orm::statements`, beside
`ModelWithPrimaryKey` / `FKFieldOf`:

```cpp
// A field type is a valid FK target iff its referenced entity (optional-unwrapped) has a
// primary key. Names the boundary find_fk_primary_key relies on and that FKFieldOf did not
// previously enforce at the call site.
template <typename FieldType>
concept ValidForeignKey = ModelWithPrimaryKey<utilities::optional_inner_type_t<FieldType>>;
```

**Deviation from the issue's signature.** The issue proposes
`ValidForeignKey<FieldType, EntityType>`. `EntityType` is unused by the body (the PK check
is purely on the FK's *target* entity, `optional_inner_type_t<FieldType>`), and the FK's
*owning* entity is already pinned by `FKFieldOf<T, Member>`. An unused concept parameter
would trip Storm's conventions (and Sonar). The concept is therefore single-parameter.

### Wiring (both sites — user-approved scope)

1. **`find_fk_primary_key<FKType>()`** — replace the inline
   `requires ModelWithPrimaryKey<utilities::optional_inner_type_t<FKType>>` with
   `requires ValidForeignKey<FKType>`. Byte-for-byte identical constraint; pure single-sourcing.

2. **`FKFieldOf<T, Member>`** — after confirming the re-derived member `m` is an FK, also
   require its target to satisfy `ValidForeignKey`. Concretely, at the point the loop matches
   `m` by identifier and returns `meta::is_fk_field(m)`, extend to:
   `return meta::is_fk_field(m) && ValidForeignKey<typename [:std::meta::type_of(m):]>;`
   This moves the PK-less-target error to the `join<^^T::field>()` call site as a clean
   constraint violation.

### BMI safety (#262)

`FKFieldOf` re-derives `m` from `^^T` (BMI-local) before any annotation read — the existing
discipline. `ValidForeignKey<[:type_of(m):]>` delegates to `ModelWithPrimaryKey`, which reads
`annotation_of_type<FieldAttr>` on the **target model's** members (`^^InnerType`) — a fresh,
non-BMI-crossed splice, exactly the pattern `find_fk_primary_key` already uses safely. No new
segfault surface. `type_of` on the re-derived `m` is a structural query (safe on BMI-crossed
reflections, same as `fk_member_points_at`).

## Verification (compile-time only)

A new test TU (`tests/test_valid_foreign_key_concept.cpp`) with `static_assert`s only —
the issue asks for real rejection, no runtime path:

- **Valid**: a real FK'd model pair. `static_assert(ValidForeignKey<Related>)` where
  `Related` is a model with a `primary` field; and the optional form
  `static_assert(ValidForeignKey<std::optional<Related>>)`.
- **Invalid**: an FK-typed target lacking a primary key →
  `static_assert(!ValidForeignKey<NoPkModel>)`, proving genuine rejection (not merely a
  substitution that happens to fail elsewhere).
- **Call-site rejection** (optional, if it compiles cleanly): a `requires`-expression
  probe that `join<^^Bad::field>()` is ill-formed when the FK target has no PK — i.e.
  `!FKFieldOf<Bad, ^^Bad::field>` for a PK-less target — demonstrating the boundary moved
  to the call site.

The test TU must be reachable by the release `storm_tests` target for clang-tidy
(new-test-TU modmap rule).

## Non-goals

- No behavior change to valid models — `find_fk_primary_key` keeps the identical constraint;
  `FKFieldOf` only *tightens* rejection of already-broken models.
- No touch to `ModelFkPoliciesValid` / `ON DELETE` policy machinery (#431) — orthogonal.
- No runtime code path; compile-time only.

## Files

- `src/orm/statements/base.cppm` — add concept; rewire `find_fk_primary_key` and `FKFieldOf`.
- `tests/test_valid_foreign_key_concept.cpp` — new static_assert TU.
- `docs/…` and `.claude/agents/*` — update if any FK/JOIN doc enumerates the guarding concepts.
