# ValidForeignKey Concept Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a named `ValidForeignKey` concept that verifies an FK field's target entity has a primary key, and wire it into `find_fk_primary_key` (single-source) and `FKFieldOf` (close the call-site gap).

**Architecture:** One single-parameter concept in `src/orm/statements/base.cppm`, reducing to the existing `ModelWithPrimaryKey` on the optional-unwrapped FK type. `find_fk_primary_key`'s inline `requires` is replaced by the concept; `FKFieldOf` is tightened to also require the FK target satisfy the concept, moving a PK-less-target error from deep in extraction to the `join<>()` call site. Compile-time only, no runtime path.

**Tech Stack:** C++26 (clang-p2996 reflection), C++ modules, GoogleTest.

## Global Constraints

- C++26, clang-p2996. Reflection annotation reads (`annotation_of_type`) only on non-BMI-crossed reflections; structural queries (`type_of`, `parent_of`, `identifier_of`, `dealias`) are safe across BMI (#262).
- Follow SonarCloud strict gate: zero new issues, zero duplication.
- Run `storm-code-reviewer` on the staged diff before EVERY code commit (project rule 13).
- Never `--no-verify`; the pre-commit hook runs format/tidy/tests/coverage.
- A new test TU must have the **release** `storm_tests` target built before clang-tidy runs (modmap-not-found otherwise).
- Concept lives in `export namespace storm::orm::statements` → reachable via `import storm;` as `storm::orm::statements::ValidForeignKey`.

---

### Task 1: Add the `ValidForeignKey` concept and its verification test

**Files:**
- Modify: `src/orm/statements/base.cppm` (insert concept after `ModelWithPrimaryKey`, ends line 197)
- Create: `tests/schema/test_valid_foreign_key_concept.cpp`

**Interfaces:**
- Consumes: `ModelWithPrimaryKey<T>` (base.cppm:189), `utilities::optional_inner_type_t<T>` (utilities.cppm:134).
- Produces: `template <typename FieldType> concept ValidForeignKey` in `storm::orm::statements`.

- [ ] **Step 1: Write the failing test**

Create `tests/schema/test_valid_foreign_key_concept.cpp`:

```cpp
#include <gtest/gtest.h>

import storm;
import std;

// Compile-time-only verification of the ValidForeignKey concept (#474). Every
// static_assert is checked at TU compile time; the runtime TEST body exists only
// so the file registers with GoogleTest and the assertions are compiled.

using storm::orm::statements::ValidForeignKey;

// A valid FK target: has a primary key.
struct Related {
    [[= storm::FieldAttr::primary]] int id{};
    std::string                         name;
};

// An FK target lacking a primary key — must be rejected.
struct NoPkModel {
    int a{};
    int b{};
};

// A self-referential model (hierarchy): its FK points at itself.
struct Node {
    [[= storm::FieldAttr::primary]] int   id{};
    [[= storm::fk<>]] std::optional<Node> parent;
};

// ---- Positive: FK whose target has a primary key -----------------------------
static_assert(ValidForeignKey<Related>);
static_assert(ValidForeignKey<std::optional<Related>>); // nullable FK unwraps to Related

// ---- Self-referential: terminates in one step, no recursion into target FKs --
static_assert(ValidForeignKey<Node>);
static_assert(ValidForeignKey<std::optional<Node>>);

// ---- Negative: FK target with no primary key must be rejected -----------------
static_assert(!ValidForeignKey<NoPkModel>);
static_assert(!ValidForeignKey<std::optional<NoPkModel>>);

TEST(ValidForeignKeyConceptTest, CompileTimeOnly) {
    // Verification is entirely in the static_asserts above; this body just gives
    // GoogleTest something to run.
    SUCCEED();
}
```

- [ ] **Step 2: Run to verify it fails to compile**

Run: `cmake --build --preset ninja-debug --target storm_tests 2>&1 | grep -i "ValidForeignKey\|error" | head`
Expected: FAIL — `no member named 'ValidForeignKey' in namespace 'storm::orm::statements'` (proves the test exercises the not-yet-added concept).

- [ ] **Step 3: Add the concept**

In `src/orm/statements/base.cppm`, immediately after the closing `}();` of `ModelWithPrimaryKey` (line 197), insert:

```cpp
    // A field type is a valid FK target iff its referenced entity (optional-unwrapped)
    // has a primary key. Names the boundary find_fk_primary_key relies on and that
    // FKFieldOf did not previously enforce at the call site (#474). Single-level: it
    // checks only the target's PK, never recursing into the target's own FKs — so a
    // self-referential (Node::parent → Node) or mutually-referential model terminates
    // in one step.
    template <typename FieldType>
    concept ValidForeignKey = ModelWithPrimaryKey<utilities::optional_inner_type_t<FieldType>>;
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build --preset ninja-debug --target storm_tests && ./build/debug/tests/storm_tests --gtest_filter='ValidForeignKeyConceptTest.*'`
Expected: PASS (1 test).

- [ ] **Step 5: Build the release test target for tidy, then review + commit**

```bash
cmake --build --preset ninja-release --target storm_tests
```
Then dispatch `storm-code-reviewer` on the staged diff, address findings, then:
```bash
git add src/orm/statements/base.cppm tests/schema/test_valid_foreign_key_concept.cpp
git commit -m "feat(474): add ValidForeignKey concept with compile-time verification"
```
(Pre-commit hook runs format/tidy/tests/coverage.)

---

### Task 2: Single-source `find_fk_primary_key` on the concept

**Files:**
- Modify: `src/orm/statements/base.cppm:466`

**Interfaces:**
- Consumes: `ValidForeignKey<FKType>` (Task 1).
- Produces: no signature change — `find_fk_primary_key<FKType>()` keeps the identical constraint, now named.

- [ ] **Step 1: Replace the inline requires-clause**

In `src/orm/statements/base.cppm`, change the constraint on `find_fk_primary_key` (line 466) from:

```cpp
            requires ModelWithPrimaryKey<utilities::optional_inner_type_t<FKType>>
```
to:
```cpp
            requires ValidForeignKey<FKType>
```

- [ ] **Step 2: Verify the whole suite still builds & passes (behavior-identical constraint)**

Run: `cmake --build --preset ninja-debug --target storm_tests && ./build/debug/tests/storm_tests --gtest_filter='*Join*:*ReverseFk*:*ManyToMany*:ValidForeignKeyConceptTest.*'`
Expected: PASS — no behavior change; join/FK-extraction tests are green.

- [ ] **Step 3: Review + commit**

Build release test target (`cmake --build --preset ninja-release --target storm_tests`), dispatch `storm-code-reviewer` on the diff, address findings, then:
```bash
git add src/orm/statements/base.cppm
git commit -m "refactor(474): single-source find_fk_primary_key on ValidForeignKey"
```

---

### Task 3: Enforce `ValidForeignKey` at the `FKFieldOf` call site (close the gap)

**Files:**
- Modify: `src/orm/statements/base.cppm:259-273` (the `FKFieldOf` concept)
- Modify: `tests/schema/test_valid_foreign_key_concept.cpp` (add call-site rejection asserts)

**Interfaces:**
- Consumes: `ValidForeignKey` (Task 1), `meta::is_fk_field` (base.cppm:69).
- Produces: `FKFieldOf<T, Member>` now additionally requires the FK target to have a PK.

- [ ] **Step 1: Add the failing call-site asserts to the test**

In `tests/schema/test_valid_foreign_key_concept.cpp`, after the existing static_asserts (before the `TEST(...)`), add:

```cpp
// ---- Call-site rejection: FKFieldOf must reject an FK whose target has no PK --
using storm::orm::statements::FKFieldOf;

struct BadOwner {
    [[= storm::FieldAttr::primary]] int id{};
    [[= storm::fk<>]] NoPkModel         ref; // FK to a PK-less target
};

struct GoodOwner {
    [[= storm::FieldAttr::primary]] int id{};
    [[= storm::fk<>]] Related           ref; // FK to a valid target
};

static_assert(FKFieldOf<GoodOwner, ^^GoodOwner::ref>);  // valid FK target
static_assert(!FKFieldOf<BadOwner, ^^BadOwner::ref>);   // PK-less target rejected at the gate
```

- [ ] **Step 2: Run to verify the new assert fails**

Run: `cmake --build --preset ninja-debug --target storm_tests 2>&1 | grep -i "static_assert\|FKFieldOf\|error" | head`
Expected: FAIL — `static_assert(!FKFieldOf<BadOwner, ^^BadOwner::ref>)` fails because current `FKFieldOf` only checks `is_fk_field` and still accepts the PK-less target (proves the gap is real).

- [ ] **Step 3: Tighten `FKFieldOf`**

In `src/orm/statements/base.cppm`, in the `FKFieldOf` concept body, change the matched-member return (line 269) from:

```cpp
                return meta::is_fk_field(m);
```
to:
```cpp
                return meta::is_fk_field(m) && ValidForeignKey<typename[:std::meta::type_of(m):]>;
```

Note: `m` is re-derived from `^^T` (BMI-local), so `type_of(m)` and the delegated
`ModelWithPrimaryKey` read on the target model are BMI-safe (mirrors `fk_member_points_at`
and `find_fk_primary_key`).

- [ ] **Step 4: Run to verify both asserts pass and no regression**

Run: `cmake --build --preset ninja-debug --target storm_tests && ./build/debug/tests/storm_tests --gtest_filter='ValidForeignKeyConceptTest.*:*Join*:*ReverseFk*:*ManyToMany*'`
Expected: PASS — call-site asserts hold; all existing join/FK tests still green (real models all have valid FK targets).

- [ ] **Step 5: Review + commit**

Build release test target, dispatch `storm-code-reviewer` on the diff, address findings, then:
```bash
git add src/orm/statements/base.cppm tests/schema/test_valid_foreign_key_concept.cpp
git commit -m "feat(474): enforce ValidForeignKey at the FKFieldOf join call site"
```

---

### Task 4: Docs + PR

**Files:**
- Modify: `docs/guide/features/JOIN_OPERATIONS.md` and/or `docs/guide/features/REFERENTIAL_INTEGRITY.md` (whichever enumerates the FK/JOIN guarding concepts) — add a one-line mention of `ValidForeignKey`.
- Modify: `CLAUDE.md` FK section only if it enumerates the guarding concepts (it currently names `ModelFkPoliciesValid` / `is_fk_field`; add `ValidForeignKey` if that list is meant to be complete — otherwise leave).

- [ ] **Step 1: Update docs**

Add a sentence to the relevant FK/JOIN doc: "`join<>`/`left_join<>` selectors are gated by `FKFieldOf`, which now also requires the FK target to have a primary key (`ValidForeignKey`, #474) — a PK-less FK target is rejected at the call site."

- [ ] **Step 2: Commit docs**

```bash
git add docs/ CLAUDE.md
git commit -m "docs(474): document ValidForeignKey FK-target guard"
```

- [ ] **Step 3: Push, PR, SonarCloud, CI, merge, close**

```bash
git push -u origin feature/474-valid-foreign-key-concept
gh pr create --base develop --title "feat(474): add ValidForeignKey concept for JOIN/FK validation" \
  --body "Closes #474"
```
Then: wait 30s → `/sonarcloud-status` (must be zero new issues) → `gh pr checks <PR#> --watch` (ninja-debug, ninja-release, ninja-asan-ubsan, ninja-tsan all green) → `gh pr merge <PR#> --squash --auto`. After merge: `gh issue close 474`, then `git checkout develop && git pull`.

- [ ] **Step 4: Verify issue subtasks**

Before merge, re-read `gh issue view 474` "Verification" section and confirm the static_assert-valid / static_assert-invalid requirement is delivered; check off any subtask boxes.

---

## Self-Review

- **Spec coverage:** concept added (Task 1); `find_fk_primary_key` single-sourced (Task 2, spec §Wiring 1); `FKFieldOf` gap closed (Task 3, spec §Wiring 2); single-parameter deviation honored (Task 1 signature); self-referential case (spec §Recursive) covered by Task 1 Node asserts; valid+invalid verification (spec §Verification) in Tasks 1 & 3; BMI-safety (spec §BMI) noted in Task 3 Step 3. ✅
- **Placeholder scan:** every code step shows full code; commands have expected output. ✅
- **Type consistency:** `ValidForeignKey` single-parameter throughout; `FKFieldOf`, `ModelWithPrimaryKey`, `optional_inner_type_t`, `is_fk_field` names match the source (verified against base.cppm:69/189/260/466, utilities.cppm:134). ✅
