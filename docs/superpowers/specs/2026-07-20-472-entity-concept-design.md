# Entity concept for compile-time model validation (#472)

## Problem

`QuerySet<T>`, statement classes, and other templates constrained on a model
type `T` rely on ad-hoc `requires` clauses or implicit reflection assumptions
rather than a named concept. A non-model type passed as `T` (e.g. `int`) fails
deep inside reflection-based code with an opaque error instead of at a clear,
named boundary.

Split out of #206 (High Priority bucket). Parent issue: #206.

## The concept

A structural check — "is `T` a reflectable class type at all?" — not a Storm
policy check. Lives in `src/orm/field_attr.cppm`, `namespace storm::meta`.

```cpp
template <typename T>
concept Entity = requires {
    { std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()) }
        -> std::same_as<std::vector<std::meta::info>>;
    { std::meta::identifier_of(^^T) } -> std::convertible_to<std::string_view>;
};
```

For `int` the `nonstatic_data_members_of(^^int, ...)` requirement is ill-formed,
so the `requires` fails cleanly — that is what makes the concept reject
non-models.

The `access_context` argument matches the existing call sites in
`statements/base.cppm` (`ModelWithPrimaryKey` et al. use
`std::meta::access_context::unchecked()`); the exact spelling the clang-p2996
build accepts is verified by building, not trusted from the issue's simplified
snippet.

## Home: `src/orm/field_attr.cppm`

Chosen over `src/db/concept.cppm` (DB-backend concepts, no model reflection) and
over a new module. `field_attr.cppm` is the dependency-free leaf module (imports
only `std`, includes `<meta>`), already sits below both `queryset.cppm` and
`base.cppm` in the import graph, and already hosts reflection helpers in
`storm::meta`. **Zero circular-dependency risk; no new module needed.**

## Relationship to the existing model concepts (kept separate)

`Entity` is the outer *structural* gate. The three concepts already on
`BaseStatement` are *semantic policy* checks that presuppose reflectability:

```
Entity<T>                 ← structural: T is a reflectable class      (new, #472)
  ModelWithPrimaryKey<T>  ← semantic: has a [[= primary]] field
  ModelStorageAnnotated<T>← semantic: every uint64 field is storage-annotated
  ModelFkPoliciesValid<T> ← semantic: every SetNull FK is nullable
```

They are **not** collapsed: `Entity` is a general "is-a-model" boundary reusable
by `QuerySet<T>` (which must not require a primary key just to exist), while the
semantic concepts form the strict model contract only statement classes need.

## Application (load-bearing)

- **`QuerySet<T>`** (`src/orm/queryset.cppm`): its `T` is currently
  unconstrained (`class T,`). Add `requires storm::meta::Entity<T>`.
- **`BaseStatement<T>`** (`src/orm/statements/base.cppm`): prepend `Entity<T>`
  to the existing requires-chain so it reads as the precondition the semantic
  concepts assume:
  ```cpp
  requires storm::meta::Entity<T> && ModelWithPrimaryKey<T>
        && ModelStorageAnnotated<T> && ModelFkPoliciesValid<T>
  ```
  Every concrete statement (insert/select/update/erase/…) flows through
  `BaseStatement<T>`, so this covers the whole statement family at one
  chokepoint — no per-statement edits.

## Verification

Compile-time only (per the issue). In a test TU, using a real model from
`tests/test_models.h`:

```cpp
static_assert(storm::meta::Entity<Person>);
static_assert(!storm::meta::Entity<int>);
```

`static_assert(!Entity<int>)` proves the concept actually rejects non-models,
not just that it accepts models.

## Testing / CI

Compile-time only — building the tests exercises the static_asserts. Plan:
full `ninja-debug` + `ninja-release` build, then the standard PR gate
(SonarCloud "Storm Strict" + CI sanitizers ninja-debug / ninja-release /
ninja-asan-ubsan / ninja-tsan). No runtime behaviour changes; no benchmark
impact (concepts are compile-time only).

## Docs

Update `CLAUDE.md` (Supported Field Types / concepts area) and the relevant
`docs/` reference to mention the `Entity` boundary. Code + docs + any touched
`.claude/agents/*.md` commit together.
