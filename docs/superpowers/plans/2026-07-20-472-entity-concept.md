# Entity Concept Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a named `storm::meta::Entity` concept — a compile-time structural gate that rejects non-model types — and make it load-bearing on `QuerySet<T>` and `BaseStatement<T>`.

**Architecture:** `Entity<T>` is a `requires`-expression concept in the dependency-free leaf module `storm_orm_field_attr` (`src/orm/field_attr.cppm`), sitting structurally *above* the existing semantic model concepts (`ModelWithPrimaryKey`, `ModelStorageAnnotated`, `ModelFkPoliciesValid`). It checks only that `T` is a reflectable class type (`nonstatic_data_members_of` / `identifier_of` are well-formed). Applied to `QuerySet<T>` (currently unconstrained) and prepended to the `BaseStatement<T>` requires-chain.

**Tech Stack:** C++26, clang-p2996 reflection (`std::meta`), CMake/Ninja presets, GoogleTest (tests auto-discovered by GLOB — no CMake edit for a new test file).

## Global Constraints

- C++26 only; custom clang-p2996 at `../clang-p2996/`.
- Reflection code needs textual `#include <meta>` BEFORE imports; `import std;` for the rest.
- Compile-time errors use `requires`/concepts, never `throw` (CLAUDE.md rule 11).
- New concept lives in `export namespace storm::meta` (matches the existing `Unsigned64` concept in the same module).
- No runtime behaviour change; no benchmark impact — concepts are compile-time only. Skip benchmarking (per memory: bench-only rule inverted — this is a non-runtime change).
- Run `storm-code-reviewer` on the staged diff BEFORE every code commit (CLAUDE.md rule 13).
- Never `--no-verify`; the pre-commit hook (`commit.sh`) runs format/tidy/tests/coverage. `ninja-debug` must be built first.
- SonarCloud "Storm Strict" gate must pass on the PR before merge.

---

## File Structure

- `src/orm/field_attr.cppm` — **modify**: add `Entity` concept inside `export namespace storm::meta` (before the closing `}` at line 206).
- `src/orm/queryset.cppm` — **modify**: add `requires storm::meta::Entity<T>` to the `QuerySet` class template (line ~39-43).
- `src/orm/statements/base.cppm` — **modify**: prepend `storm::meta::Entity<T> &&` to the `BaseStatement` requires-chain (line 384).
- `tests/schema/test_entity_concept.cpp` — **create**: compile-time `static_assert`s proving `Entity<Person>` and `!Entity<int>`, plus that the constraints are load-bearing.
- `CLAUDE.md` — **modify**: one line noting the `Entity<T>` boundary.
- `docs/guide/reference/FIELD_TYPES.md` — **modify**: brief mention of the `Entity` concept (concepts reference area).

---

## Task 1: Add the `Entity` concept

**Files:**
- Modify: `src/orm/field_attr.cppm` (insert before `} // namespace storm::meta` at line 206)
- Create: `tests/schema/test_entity_concept.cpp`

**Interfaces:**
- Produces: `storm::meta::Entity<T>` — a concept, true iff `T` is a reflectable class type (`nonstatic_data_members_of(^^T, access_context::unchecked())` yields `std::vector<std::meta::info>` and `identifier_of(^^T)` yields a `string_view`-convertible).

- [ ] **Step 1: Write the failing test**

Create `tests/schema/test_entity_concept.cpp`:

```cpp
#include <gtest/gtest.h>
#include <meta>

#include "test_db_helpers.h"

// NOLINTBEGIN(misc-const-correctness)

import storm;
import std;

#include "test_models.h" // NOSONAR cpp:S954

// ── #472: the Entity concept is a compile-time structural gate ───────────────
// Entity<T> is true iff T is a reflectable class type (models qualify; scalars,
// pointers, and functions do not). It is the outer boundary layered above the
// semantic model concepts (ModelWithPrimaryKey, etc.).

// A real model satisfies Entity.
static_assert(storm::meta::Entity<Person>);
static_assert(storm::meta::Entity<Message>);

// Non-class / non-model types are rejected — proves the concept actually gates.
static_assert(!storm::meta::Entity<int>);
static_assert(!storm::meta::Entity<double>);
static_assert(!storm::meta::Entity<int*>);
static_assert(!storm::meta::Entity<void()>);

// NOLINTEND(misc-const-correctness)

TEST(EntityConcept, CompileTimeOnly) {
    // The static_asserts above are the real test; this keeps the TU a runnable
    // GTest target so the suite reports it.
    SUCCEED();
}
```

- [ ] **Step 2: Configure + build to verify it fails**

Run:
```bash
cmake --preset ninja-debug && cmake --build --preset ninja-debug 2>&1 | tail -30
```
Expected: FAIL — compile error at `static_assert(storm::meta::Entity<Person>)` / use of undeclared `Entity` (concept does not exist yet).

- [ ] **Step 3: Add the concept**

In `src/orm/field_attr.cppm`, immediately before the closing `} // namespace storm::meta` (line 206), add:

```cpp
    // Issue #472: Entity is the compile-time STRUCTURAL gate for model types —
    // "is T a reflectable class type at all?". It is layered ABOVE the semantic
    // model concepts (ModelWithPrimaryKey / ModelStorageAnnotated /
    // ModelFkPoliciesValid in statements/base.cppm), which presuppose
    // reflectability. Constraining QuerySet<T> and BaseStatement<T> with it makes
    // a non-model T (int, a pointer, a function type) fail at this named boundary
    // instead of deep inside reflection-based code. For a non-class type the
    // nonstatic_data_members_of requirement is ill-formed, so the requires fails
    // cleanly. access_context::unchecked() matches the semantic concepts' call
    // sites in statements/base.cppm.
    template <typename T>
    concept Entity = requires {
        {
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())
        } -> std::same_as<std::vector<std::meta::info>>;
        { std::meta::identifier_of(^^T) } -> std::convertible_to<std::string_view>;
    };
```

- [ ] **Step 4: Build to verify the test passes**

Run:
```bash
cmake --build --preset ninja-debug 2>&1 | tail -20
```
Expected: PASS — build succeeds; the `static_assert`s compile. If `same_as<std::vector<std::meta::info>>` is rejected by this clang-p2996 build's exact return type, relax that one requirement to `-> std::ranges::range` or drop the trailing-return-type constraint (keep the requirement itself so `int` is still ill-formed) and rebuild. Verify with:
```bash
ctest --preset ninja-debug -R EntityConcept
```
Expected: 1 test passes.

- [ ] **Step 5: Review + commit**

Dispatch `storm-code-reviewer` on the staged diff; address findings. Then:
```bash
git add src/orm/field_attr.cppm tests/schema/test_entity_concept.cpp
git status --short   # show files, get approval
git commit -m "feat(472): add storm::meta::Entity structural concept

Compile-time gate rejecting non-model types (int, pointers, functions).
static_assert(Entity<Person>) + static_assert(!Entity<int>) prove it gates.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: Constrain `QuerySet<T>` with `Entity<T>`

**Files:**
- Modify: `src/orm/queryset.cppm` (class template at line ~39-43)
- Modify: `tests/schema/test_entity_concept.cpp` (add a load-bearing assertion)

**Interfaces:**
- Consumes: `storm::meta::Entity<T>` (Task 1). `queryset.cppm` already transitively imports `storm_orm_field_attr`.
- Produces: `QuerySet<T>` now requires `Entity<T>` — instantiating `QuerySet<int>` is ill-formed.

- [ ] **Step 1: Add the failing load-bearing test**

Append to `tests/schema/test_entity_concept.cpp`, after the existing `static_assert`s (before `// NOLINTEND`):

```cpp
// The constraint is load-bearing: QuerySet is instantiable for a real model and
// NOT for a non-model type (the requires clause rejects it, not deep reflection).
static_assert(requires { typename storm::QuerySet<Person>; });
static_assert(!requires { typename storm::QuerySet<int>; });
```

- [ ] **Step 2: Build to verify it fails**

Run:
```bash
cmake --build --preset ninja-debug 2>&1 | tail -20
```
Expected: FAIL — `static_assert(!requires { typename storm::QuerySet<int>; })` fires, because `QuerySet<int>` is currently a valid type (T is unconstrained).

- [ ] **Step 3: Add the constraint**

In `src/orm/queryset.cppm`, change the class template head:

```cpp
    template <
            class T,
            storm::db::DatabaseConnection ConnType   = storm::db::sqlite::Connection,
            bool                          Finalized_ = false>
        requires storm::meta::Entity<T>
    class QuerySet { // NOSONAR(cpp:S1448) — ORM facade class; method count grows with supported operations
```

- [ ] **Step 4: Build + test to verify it passes**

Run:
```bash
cmake --build --preset ninja-debug 2>&1 | tail -20 && ctest --preset ninja-debug -R EntityConcept
```
Expected: build succeeds; EntityConcept test passes. If unrelated TUs fail to compile because they instantiate `QuerySet<Something>` where `Something` is not a model, that is a real find — report it before working around it.

- [ ] **Step 5: Run the full suite to confirm no regression**

Run:
```bash
ctest --preset ninja-debug 2>&1 | tail -15
```
Expected: all tests pass (PG skips gracefully if not running).

- [ ] **Step 6: Review + commit**

Dispatch `storm-code-reviewer`; address findings. Then:
```bash
git add src/orm/queryset.cppm tests/schema/test_entity_concept.cpp
git status --short
git commit -m "feat(472): constrain QuerySet<T> with Entity<T>

QuerySet<int> is now ill-formed at the class boundary instead of failing
deep inside reflection code.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: Prepend `Entity<T>` to the `BaseStatement<T>` requires-chain

**Files:**
- Modify: `src/orm/statements/base.cppm` (requires-chain at line 384)

**Interfaces:**
- Consumes: `storm::meta::Entity<T>` (Task 1). `base.cppm` already imports `storm_orm_field_attr` transitively (via `storm_orm_relation_meta` → `storm_orm_field_attr`); confirm `storm::meta::Entity` resolves. If it does not, add `import storm_orm_field_attr;` is NOT needed — it's already in the graph; the symbol is in `storm::meta`.
- Produces: `BaseStatement<T>` now requires `Entity<T>` as the first clause; every concrete statement inherits the gate.

- [ ] **Step 1: Add the constraint**

In `src/orm/statements/base.cppm`, change line 383-384:

```cpp
    template <typename T>
        requires storm::meta::Entity<T> && ModelWithPrimaryKey<T> && ModelStorageAnnotated<T> &&
                 ModelFkPoliciesValid<T>
    class BaseStatement {
```

(The semantic concepts `ModelWithPrimaryKey` etc. are in `storm::orm::statements`; `Entity` is fully qualified `storm::meta::Entity`.)

- [ ] **Step 2: Build to verify it still compiles**

Run:
```bash
cmake --build --preset ninja-debug 2>&1 | tail -20
```
Expected: build succeeds. `Entity<T>` is strictly weaker than `ModelWithPrimaryKey<T>` (a model with a PK is always a reflectable class), so no real model is newly rejected — the change is a clearer error *ordering* plus a shared named boundary.

- [ ] **Step 3: Run the full suite**

Run:
```bash
ctest --preset ninja-debug 2>&1 | tail -15
```
Expected: all tests pass.

- [ ] **Step 4: Review + commit**

Dispatch `storm-code-reviewer`; address findings. Then:
```bash
git add src/orm/statements/base.cppm
git status --short
git commit -m "feat(472): layer Entity<T> as outer gate of BaseStatement requires-chain

Entity<T> is the structural precondition the semantic model concepts assume;
covers the whole statement family at one chokepoint.

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 4: Docs

**Files:**
- Modify: `CLAUDE.md`
- Modify: `docs/guide/reference/FIELD_TYPES.md`

- [ ] **Step 1: Update CLAUDE.md**

In the "Supported Field Types" / concepts area of `CLAUDE.md`, add a short paragraph (match surrounding `**Bold (#NNN)**:` style):

```markdown
**Entity concept (#472)**: `storm::meta::Entity<T>` is the compile-time structural
gate for model types — true iff `T` is a reflectable class. `QuerySet<T>` and
`BaseStatement<T>` require it, so a non-model `T` (e.g. `int`) fails at that named
boundary instead of deep inside reflection code. It sits above the semantic model
concepts (`ModelWithPrimaryKey`, `ModelStorageAnnotated`, `ModelFkPoliciesValid`),
which stay separate — they check model *policy*, `Entity` checks *reflectability*.
```

- [ ] **Step 2: Update FIELD_TYPES.md**

Add an equivalent short note to `docs/guide/reference/FIELD_TYPES.md` in the concepts/validation section (read the file first to match its heading style).

- [ ] **Step 3: Commit (docs-only — no reviewer, no code hooks)**

```bash
git add CLAUDE.md docs/guide/reference/FIELD_TYPES.md
git status --short
git commit -m "docs(472): document the Entity concept boundary

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 5: PR + gate

- [ ] **Step 1: Verify subtasks on the issue**

```bash
gh issue view 472   # confirm Definition-of-done items; check them off with gh issue edit if present
```

- [ ] **Step 2: Push + open PR**

```bash
git push -u origin feature/472-entity-concept
gh pr create --base develop --title "feat(472): Entity concept for compile-time model validation" --body "$(cat <<'EOF'
Closes #472

Adds `storm::meta::Entity<T>` — a compile-time structural gate rejecting
non-model types — and makes it load-bearing on `QuerySet<T>` and
`BaseStatement<T>`. Layered above (not merged with) the existing semantic model
concepts. Compile-time only; no runtime/benchmark impact.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

- [ ] **Step 3: SonarCloud + CI gate**

Wait 30s, then run `/sonarcloud-status`. Fix any new-code issues on the branch and re-check until clean. Then `gh pr checks <PR#> --watch`. Merge with `gh pr merge --squash --auto` only after both the SonarCloud gate and all CI jobs (ninja-debug, ninja-release, ninja-asan-ubsan, ninja-tsan) pass.

- [ ] **Step 4: Post-merge**

```bash
gh issue close 472
git checkout develop && git pull
git worktree remove ../worktrees/feature-472-entity-concept   # from the main repo dir
```

---

## Self-Review Notes

- **Spec coverage:** concept (Task 1) ✓, home = field_attr.cppm ✓, QuerySet constraint (Task 2) ✓, BaseStatement constraint (Task 3) ✓, `static_assert(Entity<Person>)` + `!Entity<int>` (Task 1) ✓, kept-separate layering (Task 3 + docs) ✓, docs (Task 4) ✓.
- **Placeholder scan:** all code steps show full code; no TBD/TODO.
- **Type consistency:** `storm::meta::Entity<T>` used identically in Tasks 1–3; `access_context::unchecked()` matches existing call sites; `EntityConcept` test name consistent.
- **Risk noted inline:** Step 4 of Task 1 has a fallback if the `same_as<std::vector<...>>` return-type constraint is rejected by this clang build.
