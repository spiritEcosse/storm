# #363 — Converge execute paths on a shared prepare→reset→bind skeleton

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Hoist the repeated `prepare_cached → reset → bind → return Statement*` skeleton in statement classes into shared per-file helpers (the `setop::ready_statement` model), reducing duplication without regressing Release benchmarks.

**Architecture:** Behavior-preserving refactor. Each converted file gets one private `ready_statement(...)`-style helper that owns the prepare→reset→bind prefix; callers become `ready_statement(...) → check → run`. Explicit `if (!x) return std::unexpected(...)` control flow is kept — `and_then` was benched and regresses hot paths by up to +2.6% (see spec). Row loops stay flat and untouched.

**Tech Stack:** C++26 modules, clang-p2996, `std::expected`, CMake `ninja-release` / `ninja-debug` / sanitizer presets, Google Benchmark (`storm_bench`).

---

## Pre-flight: scope narrowed during planning

Reading the actual code narrowed the in-scope set. The spec's YAGNI rule (no helper for a single call site) applies:

- **erase.cppm** — `prepare_cached → check → reset` prefix repeats in 3 sites (`to_sql(obj)`, `to_sql(span)`, `execute_one`). **Genuine dedup → Task 2.**
- **aggregate.cppm** — `prepare_bind_extract` + `prepare_bind_having_extract` both repeat the `prepare_cached → check` prefix. **Genuine dedup → Task 3.**
- **update.cppm** — already has `prepare_stmt` + `reset_bind_execute` (#277); only 3 thin `prepare_cached → check` prefixes remain, each feeding different bind logic. **Leave alone** (documented in PR; converting adds indirection for ~no dedup).
- **distinct.cppm** — exactly ONE `prepare_cached → check → bind → loop` site. A helper for one caller is not dedup. **Leave alone** (documented in PR).
- **select.cppm / insert.cppm** — already clean (`prepare_and_bind`). **Leave alone.**
- **setop.cppm** — the model. **Unchanged.**
- **PG** (`postgresql_*.cppm`) — **deferred to follow-up issue** (Task 6).

Net: two files actually change (erase, aggregate). This is the honest dedup surface.

---

## File Structure

- Modify: `src/orm/statements/erase.cppm` — add private `ready_delete_statement(sql)` helper; route 3 prefixes through it.
- Modify: `src/orm/statements/aggregate.cppm` — add private `ready_aggregate_statement(sql)` helper; route both `prepare_bind_*` through it.
- No CMake changes (no new files; `.cppm` already globbed).
- No new tests (behavior-preserving; existing suite + bench + sanitizers are the net).

---

## Task 1: Establish the develop baseline (gate reference)

**Files:** none (measurement only).

- [ ] **Step 1: Confirm clean tree on the feature branch**

Run: `git status --short`
Expected: clean (spec + plan committed, no source changes).

- [ ] **Step 2: Build Release**

Run: `cmake --preset ninja-release && cmake --build --preset ninja-release`
Expected: build succeeds (exit 0).

- [ ] **Step 3: Record baseline medians for the gated filters**

Run:
```bash
./build/release/benchmarks/storm_bench \
  --benchmark_filter='Storm/(DELETE|INSERT|.*[Aa]ggregate|COUNT|SUM|AVG|MIN|MAX|GROUP).*' \
  --benchmark_repetitions=15 --benchmark_report_aggregates_only=true \
  --benchmark_format=console 2>/dev/null | tee /tmp/363_baseline.txt | grep -E "median|_cv"
```
Expected: a table of `*_median` + `*_cv` rows. Confirm every `_cv` < ~2% (gate resolvable; per memory `feedback-bench-aggregates-trust-signal`). Save `/tmp/363_baseline.txt`.

- [ ] **Step 4: Confirm full suite green on baseline**

Run: `cmake --preset ninja-debug && cmake --build --preset ninja-debug && ctest --preset ninja-debug --output-on-failure`
Expected: all tests pass. (This proves the net is green before any change.)

---

## Task 2: erase.cppm — hoist the delete-statement prefix

**Files:**
- Modify: `src/orm/statements/erase.cppm` — add helper near `bind_pk_at` (~line 411); route `to_sql(obj)` (231), `to_sql(span)` (245), `execute_one` (301).

The repeated prefix is: `conn_->prepare_cached(sql) → if(!r) return unexpected → *r → ->reset()`. The helper returns the reset, ready Statement* (no bind — callers bind differently: single PK, span of PKs, or none).

- [ ] **Step 1: Add the private helper**

Add inside the class, in the private section near `bind_pk_at` (after line ~409):

```cpp
        // Prepare via the L3 cache and reset — the prepare→check→reset prefix
        // shared by to_sql(obj), to_sql(span) and execute_one(). Binding stays
        // at the call site (single PK / span of PKs / none differ per caller).
        // Explicit-check form (not and_then): monadic chaining benched slower
        // on hot delete paths (#363).
        [[nodiscard]] __attribute__((always_inline)) auto ready_delete_statement(const std::string& sql) noexcept
                -> std::expected<Statement*, Error> {
            auto stmt_result = conn_->prepare_cached(sql);
            if (!stmt_result) [[unlikely]] {
                return std::unexpected(stmt_result.error());
            }
            Statement* stmt = *stmt_result;
            stmt->reset();
            return stmt;
        }
```

- [ ] **Step 2: Route `to_sql(const T& obj)` (lines 231-242) through the helper**

Replace:
```cpp
        [[nodiscard]] auto to_sql(const T& obj) -> std::expected<std::string, Error> {
            auto stmt_result = conn_->prepare_cached(get_single_delete_sql());
            if (!stmt_result) {
                return std::unexpected(stmt_result.error());
            }
            auto* stmt = *stmt_result;
            stmt->reset();
            if (auto bind_result = bind_pk_at(*stmt, obj, 1); !bind_result) {
                return std::unexpected(bind_result.error());
            }
            return stmt->expanded_sql();
        }
```
With:
```cpp
        [[nodiscard]] auto to_sql(const T& obj) -> std::expected<std::string, Error> {
            auto stmt_result = ready_delete_statement(get_single_delete_sql());
            if (!stmt_result) {
                return std::unexpected(stmt_result.error());
            }
            auto* stmt = *stmt_result;
            if (auto bind_result = bind_pk_at(*stmt, obj, 1); !bind_result) {
                return std::unexpected(bind_result.error());
            }
            return stmt->expanded_sql();
        }
```

- [ ] **Step 3: Route `to_sql(std::span<const T> objects)` (lines 245-264) through the helper**

Replace:
```cpp
            const auto& bulk_sql = get_bulk_delete_sql(objects.size());
            auto        stmt_res = conn_->prepare_cached(bulk_sql);
            if (!stmt_res) {
                return std::unexpected(stmt_res.error());
            }
            auto* stmt = *stmt_res;
            stmt->reset();
            int param_index = 1;
```
With:
```cpp
            const auto& bulk_sql  = get_bulk_delete_sql(objects.size());
            auto        stmt_res  = ready_delete_statement(bulk_sql);
            if (!stmt_res) {
                return std::unexpected(stmt_res.error());
            }
            auto* stmt        = *stmt_res;
            int   param_index = 1;
```

- [ ] **Step 4: Route `execute_one(const T& obj)` (lines 301-322) through the helper**

Replace:
```cpp
            auto stmt_result = conn_->prepare_cached(get_single_delete_sql());
            if (!stmt_result) {
                return std::unexpected(stmt_result.error());
            }
            auto* stmt = *stmt_result;

            // Reset, bind PK, and execute
            stmt->reset();

            if (auto bind_result = bind_pk_at(*stmt, obj, 1); !bind_result) {
```
With:
```cpp
            auto stmt_result = ready_delete_statement(get_single_delete_sql());
            if (!stmt_result) {
                return std::unexpected(stmt_result.error());
            }
            auto* stmt = *stmt_result;

            // Bind PK and execute (statement already reset by ready_delete_statement)
            if (auto bind_result = bind_pk_at(*stmt, obj, 1); !bind_result) {
```

- [ ] **Step 5: Build Release and run the DELETE bench A/B**

Run:
```bash
cmake --build --preset ninja-release && \
./build/release/benchmarks/storm_bench \
  --benchmark_filter='Storm/DELETE/.*' --benchmark_repetitions=15 \
  --benchmark_report_aggregates_only=true --benchmark_format=console 2>/dev/null \
  | grep -E "median|_cv"
```
Expected: medians within cv-noise of `/tmp/363_baseline.txt` DELETE rows (no median slowdown beyond ~1.5%, i.e. > 2–3× the cv). **If ANY DELETE median regresses beyond noise: `git checkout src/orm/statements/erase.cppm` and STOP — report the regression.**

- [ ] **Step 6: Build debug + run erase tests**

Run:
```bash
cmake --build --preset ninja-debug && \
./build/debug/tests/storm_tests --gtest_filter='*Delete*:*Erase*:*Remove*'
```
Expected: all pass. (If filter matches nothing, run the full `ctest --preset ninja-debug`.)

- [ ] **Step 7: Commit**

```bash
git add src/orm/statements/erase.cppm
git commit -m "refactor(363): hoist delete prepare→reset prefix into ready_delete_statement

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```
(Pre-commit hook runs format/tidy/tests/coverage automatically — let it.)

---

## Task 3: aggregate.cppm — hoist the prepare→check prefix

**Files:**
- Modify: `src/orm/statements/aggregate.cppm` — `prepare_bind_extract` (~452) and `prepare_bind_having_extract` (~474) both open with `conn_->prepare_cached(sql) → if(!r) return unexpected`. Add a `ready_aggregate_statement(sql)` helper returning the prepared (already-reset-on-cache-hit) Statement*.

- [ ] **Step 1: Re-read the two helpers to confirm exact current text**

Run: `sed -n '443,490p' src/orm/statements/aggregate.cppm`
Expected: see `prepare_bind_extract` and `prepare_bind_having_extract` with the `auto prepare_result = conn_->prepare_cached(sql); if (!prepare_result) [[unlikely]] return std::unexpected(prepare_result.error());` prefix in each. (Confirm line numbers/text before editing — they may have shifted; match what's actually there.)

- [ ] **Step 2: Add the private helper**

Add in the private section just above `prepare_bind_extract`:

```cpp
        // Shared prepare→check prefix for the bind+extract helpers below.
        // prepare_cached already resets the statement on a cache hit, so no
        // explicit reset here. Explicit-check form (not and_then): monadic
        // chaining benched slower on aggregate paths (#363).
        [[nodiscard]] __attribute__((always_inline)) auto ready_aggregate_statement(const std::string& sql) noexcept
                -> std::expected<Statement*, Error> {
            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) [[unlikely]] {
                return std::unexpected(prepare_result.error());
            }
            return *prepare_result;
        }
```

- [ ] **Step 3: Route `prepare_bind_extract` through the helper**

Replace its opening prefix:
```cpp
        [[nodiscard]] auto prepare_bind_extract(const std::string& sql) -> std::expected<ResultType, Error> {
            auto prepare_result = conn_->prepare_cached(sql);
            if (!prepare_result) {
                return std::unexpected(prepare_result.error());
            }
```
With:
```cpp
        [[nodiscard]] auto prepare_bind_extract(const std::string& sql) -> std::expected<ResultType, Error> {
            auto prepare_result = ready_aggregate_statement(sql);
            if (!prepare_result) {
                return std::unexpected(prepare_result.error());
            }
```
(All downstream uses of `*prepare_result` stay valid — the helper returns `Statement*` just like the old `*prepare_result`. Confirm the body still dereferences `*prepare_result` to get the `Statement*`; if it used `*prepare_result` as the pointer directly, it remains correct since the helper's value type is `Statement*`.)

- [ ] **Step 4: Route `prepare_bind_having_extract` through the helper**

Replace its opening prefix identically (the same two-to-four lines): swap `conn_->prepare_cached(sql)` for `ready_aggregate_statement(sql)` and keep the `if (!prepare_result) return std::unexpected(prepare_result.error());` check.

- [ ] **Step 5: Build Release and run the aggregate bench A/B**

Run:
```bash
cmake --build --preset ninja-release && \
./build/release/benchmarks/storm_bench \
  --benchmark_filter='Storm/(COUNT|SUM|AVG|MIN|MAX|GROUP|.*[Aa]ggregate).*' \
  --benchmark_repetitions=15 --benchmark_report_aggregates_only=true \
  --benchmark_format=console 2>/dev/null | grep -E "median|_cv"
```
Expected: medians within cv-noise of `/tmp/363_baseline.txt` aggregate rows. **If ANY median regresses beyond noise: `git checkout src/orm/statements/aggregate.cppm` and STOP — report the regression.**

- [ ] **Step 6: Build debug + run aggregate tests**

Run:
```bash
cmake --build --preset ninja-debug && \
./build/debug/tests/storm_tests --gtest_filter='*Aggregate*:*Count*:*Sum*:*Avg*:*Group*:*Having*'
```
Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add src/orm/statements/aggregate.cppm
git commit -m "refactor(363): hoist aggregate prepare→check prefix into ready_aggregate_statement

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 4: Full regression net (suite + sanitizers)

**Files:** none (verification).

- [ ] **Step 1: Full test suite (SQLite + mock PG)**

Run: `ctest --preset ninja-debug --output-on-failure`
Expected: all tests pass.

- [ ] **Step 2: ASAN + UBSAN**

Run: `cmake --preset ninja-asan-ubsan && cmake --build --preset ninja-asan-ubsan && ctest --preset ninja-asan-ubsan --output-on-failure`
Expected: no new ASAN/UBSAN reports. (Per memory, allow `timeout 900+`; ASAN is 4–5× slower.)

- [ ] **Step 3: TSAN**

Run: `cmake --preset ninja-tsan && cmake --build --preset ninja-tsan && ctest --preset ninja-tsan --output-on-failure`
Expected: no new data-race reports.

- [ ] **Step 4: Final full Release bench vs baseline**

Run the Task 1 Step 3 command again; diff medians against `/tmp/363_baseline.txt`. Expected: every gated median within cv-noise. Save the after-table for the PR.

---

## Task 5: Docs + follow-up issue + PR

**Files:**
- Possibly modify: any doc that documents the execute-path pattern (grep first).

- [ ] **Step 1: Check whether the execute-path pattern is documented anywhere**

Run: `grep -rln "ready_statement\|prepare_and_bind\|prepare→bind\|prepare_bind_extract" docs/ .claude/agents/`
Expected: if hits, update them to mention `ready_delete_statement` / `ready_aggregate_statement` as part of the same family. If no hits, no doc change needed (note that in the PR).

- [ ] **Step 2: File the PG follow-up issue**

Run:
```bash
gh issue create --title "refactor: converge PG prepare/exec paths on shared prepare→bind skeleton (#363 follow-up)" \
  --label "code-quality,refactor,tech-debt" \
  --body "Follow-up to #363. Apply the same prepare→reset→bind skeleton dedup (ready_statement model, explicit-check form — NOT and_then, which #363 benched as a regression) to the clean Shape-A paths in postgresql_connection.cppm / postgresql_statement.cppm. Not on the SQLite benchmarked hot path, so it was deferred from #363 to keep that PR bench-clean. No behavior change; PG tests are the net."
```
Expected: issue URL printed. Record the number.

- [ ] **Step 3: Update the issue #363 Definition-of-done checkboxes**

Run `gh issue view 363` to read the body, then `gh issue edit 363 --body "..."` checking off the delivered items (skeleton hoisted; shape-B untouched; PG deferred-with-issue; bench attached; sanitizers green; title-vs-model reframing documented). Leave unchecked anything not delivered.

- [ ] **Step 4: Push and open the PR**

Run:
```bash
git push -u origin feature/363-converge-execute-skeleton
gh pr create --base develop --title "refactor(363): converge execute paths on shared prepare→reset→bind skeleton" \
  --body "$(cat <<'EOF'
Closes #363.

## What
Hoists the repeated `prepare_cached → reset → bind` skeleton into shared per-file helpers (`ready_delete_statement` in erase.cppm, `ready_aggregate_statement` in aggregate.cppm), modeled on `setop::ready_statement`.

## Why NOT and_then (issue title is misleading)
The issue title says "monadic and_then". Its own cited model `setop::ready_statement` does NOT use and_then — it hoists the skeleton and keeps explicit `if(!x) return std::unexpected`. I benched the literal and_then rewrite on the `Storm/SELECT/select` hot path: it regressed EVERY size (+1.9% @N=100, +2.6% @N=1000, 3–4× the cv noise floor). So this PR does skeleton-dedup with explicit checks; row loops stay flat. Table below.

## Scope
- Changed: erase.cppm, aggregate.cppm (the real dedup surface).
- Left clean: select.cppm, insert.cppm (already have prepare_and_bind), setop.cppm (the model), update.cppm (already has prepare_stmt/reset_bind_execute — only thin prefixes remain), distinct.cppm (single call site — a helper would be indirection, not dedup).
- Shape-B untouched (documented): pool.cppm checkout, transaction.cppm commit, queryset.cppm capture_operand.
- PG paths deferred to #<follow-up>.

## Bench (Release, 15 reps)
<paste baseline-vs-after median table; confirm within cv-noise>

## Sanitizers
ASAN/UBSAN/TSAN green.
EOF
)"
```
Expected: PR URL.

- [ ] **Step 5: SonarCloud gate + CI**

Wait 30s, then run `/sonarcloud-status`. If zero new issues, run `gh pr checks <PR#> --watch`. Merge with `--squash` only after both pass. Fix-and-recheck loop if not. Then `gh issue close 363`, `git checkout develop && git pull`.

---

## Self-review notes

- **Spec coverage:** skeleton hoist (Tasks 2–3) ✓; select/insert/setop left clean ✓; shape-B untouched+documented (Task 5 PR body) ✓; PG deferred-with-issue (Task 5 Step 2) ✓; Release A/B bench (Tasks 1/2/3/4) ✓; sanitizers (Task 4) ✓; title reframing documented (Task 5 PR body) ✓.
- **update.cppm / distinct.cppm dropped from active conversion** vs the spec's "in scope" list — justified by YAGNI (single/already-helpered call sites). Documented in PR. If a reviewer insists, they're a trivial follow-up.
- **Placeholders:** none — every code step shows real before/after.
- **Type consistency:** both helpers return `std::expected<Statement*, Error>`; callers keep their existing `if (!x) return std::unexpected(x.error())` + `*x` deref. `Statement` and `Error` are the existing class type aliases.
