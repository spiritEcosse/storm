# Compile-Time Analysis

Where Storm's build time actually goes, measured rather than assumed, and what
moves it. All numbers below are measurements on a 4-core dev container
(clang-p2996, `ninja-debug` unless stated), reproducible with the methods in
[Method](#method).

**Summary**: the build is dominated by the ~110 hand-written test TUs, not by
the YAML-driven test corpus. Each test TU pays a **~5.8 s fixed floor** before
its first assertion, and that floor is **entirely frontend** — Backend is 0.03 s.

---

## Where the time goes

Object compiles only, ninja edges deduplicated (a C++ module edge emits both
`.pcm` and `.o` and must be counted once):

| bucket | files | sec | % | avg |
|---|---:|---:|---:|---:|
| hand-written test TUs | 110 | 2558 | **85.2%** | 23.3s |
| storm library modules | 35 | 216 | 7.2% | 6.2s |
| YAML corpus TUs | 4 | 159 | **5.3%** | 39.7s |
| mock test binaries | 6 | 54 | 1.8% | 9.0s |

Total ≈ **3003 s** of compile CPU. Release is ~32% more expensive again (3954 s)
despite Debug carrying coverage instrumentation — `-O2` over this much template
instantiation costs more than the instrumentation does.

By test directory: `query/` 42.6%, `schema/` 24.3%, `crud/` 19.3%, `yaml/` 6.2%.

## The per-TU floor

Measured by compiling probe TUs whose entire body is `int p(){return 0;}`
(min of 3 runs each, warm):

| probe | sec | delta |
|---|---:|---|
| empty | 0.0 | — |
| `#include <gtest/gtest.h>` | 2.2 | **+2.2** |
| + `import std;` | 2.4 | +0.2 |
| + `import storm;` | 4.3 | **+2.1** |
| + `test_db_helpers.h` + `shared/models.h` | 5.8 | **+1.4** |

`-ftime-trace` on a TU containing one `EXPECT_TRUE(true)`: Frontend 6.28 s,
**Backend 0.03 s**. Parsing (`Source`) is 3.09 s of it; the rest is 448 function
and 1197 class instantiations, in a file that instantiates nothing of its own.

Two counter-intuitive results worth keeping:

- **A module import is nearly free until something uses it.** `import std;` and
  `import storm;` in isolation measure 0.0 s — BMI loading is lazy. The 2.1 s
  above appears only *alongside* gtest: the cost is reconciling storm's BMI
  against the textual libc++ declarations gtest drags in, not deserialising the
  BMI as such.
- **Inside storm, the cost is concentrated.** Over the gtest baseline:
  `storm_db_sqlite` +0.0 s, `storm_orm_utilities` +0.2 s,
  `storm_orm_statements_join` +0.8 s, `storm_orm_schema` +1.0 s,
  **`storm_orm_queryset` +1.6 s** — of a +2.1 s total for the umbrella `storm`.

## The YAML corpus is not the problem

It registers **494 of the suite's 3093 tests (16%) for 5.3% of compile time**.
Inside the heaviest of its four TUs (36.5 s), `-ftime-trace` splits as: template
instantiation ~55%, codegen ~20%, and the consteval JSON parsing that the corpus
is often blamed for — `EvaluateAsConstantExpr` 1.6 s + `EvaluateAsInitializer`
2.8 s — **~5-8%**.

Per registered test the corpus costs 0.32 s against ~0.98 s for hand-written
tests, but that comparison is confounded: hand-written TUs pay the fixed floor
110 times and test structurally heavier things. The honest statement is that the
whole question is bounded by ~160 s in either direction, so **compile time is not
a good reason to keep or drop the corpus**.

Note that the cost driver is the number of *distinct compile-time queries*, not
the corpus format. Hand-porting the same cases pays the same instantiation.

---

## What was tried and rejected

Each of these looked promising and was killed by measurement.

### ccache — unsafe with C++20 modules

Test TUs cache beautifully in isolation (9083 ms → 43 ms, 100% cacheable, direct
hit; `.cppm` compiles are 100% uncacheable and fall through at no cost). But
**ccache does not hash the BMIs a TU imports** — clang omits them from the
depfile. Editing `src/orm/queryset.cppm` and recompiling a test TU through
ccache returns a HIT with a byte-identical object built against the *previous*
module interface:

```
before editing the module : md5=7a7510190425  hits=1
after  editing the module : md5=7a7510190425  hits=2
```

A `src/**/*.cppm` edit could therefore produce a green `commit.sh` run over test
objects compiled against stale module interfaces. Separately, ccache does not
track `#embed` either (a mutated corpus JSON still returned a HIT);
`CCACHE_EXTRAFILES` fixes that half, but nothing fixes the BMI half today.

### UNITY_BUILD — mutually exclusive with module scanning

CMake **silently** disables `UNITY_BUILD` whenever `CXX_SCAN_FOR_MODULES` is ON,
with no warning:

```
CMAKE_CXX_SCAN_FOR_MODULES=OFF -> "Unity" edges in build.ninja: 4
CMAKE_CXX_SCAN_FOR_MODULES=ON  -> "Unity" edges in build.ninja: 0
```

Scanning cannot be turned off for these TUs, because a TU with `import std;` and
`CXX_SCAN_FOR_MODULES OFF` fails with `fatal error: module 'std' not found` — no
module map is generated for it.

### `extern template` — no effect on time

It works mechanically (object 10.9 MB → 10.2 MB, symbols 8404 → 7736) but does
not move compile time, because it suppresses codegen and codegen is 0.03 s of the
floor. An early "-47%" reading was a cold-cache artifact of the first compile in
a session; with warm caches and min-of-3 the effect is zero or slightly negative.

### Replacing GoogleTest — poor return

The ceiling is gtest's 2.2 s, but any framework includes standard headers
textually, so the +2.1 s storm/libc++ reconciliation stays regardless. That
ceiling is available from a PCH instead, without rewriting 3093 tests.

---

## What was fixed

| change | effect |
|---|---|
| removed dead `create_table_sql` globals from `tests/test_models.h` (referenced by nothing, but forced full consteval schema generation in all 89 including TUs) | −0.7 s/TU ≈ **−62 s** |
| `#embed` corpora declared as real build inputs (`OBJECT_DEPENDS`) | correctness |
| precompiled `<gtest/gtest.h>` for `storm_tests` | −2.15 s/TU ≈ **−220 s** |
| `run_clang_tidy.sh --diff` no longer reports a crashed clang-tidy as a clean pass | correctness |

≈ **−280 s of ~3000 s (−9%)**.

### Two correctness bugs found along the way

**`#embed` is invisible to the dependency scanner.** Clang does not list an
embedded file in the depfile, so ninja recorded no edge from the consuming object
to the JSON (`ninja -t deps` showed none). Editing `unified_cases.yaml`
regenerated the JSON and left the objects stale — the suite kept running the
previous corpus, green. The benchmark corpus had the identical bug.

**A crashed clang-tidy read as success.** `--diff` counted only `": error:"` and
`": warning:"` lines, and a SIGSEGV prints neither. `--full` already treated an
unexpected crash as failure; `--diff` did not.

### A build-state trap worth recognising

If a build is interrupted (Ctrl-C, OOM kill, a killed CI job), ninja's
`.ninja_deps` can be left truncated. Every subsequent build then prints

```
ninja: warning: premature end of file; recovering
```

and rebuilds the whole `import storm;` dependency graph despite unchanged
sources — indistinguishable from "compilation is just slow". `ninja -t recompact`
followed by **one build that runs to completion uninterrupted** restores true
incremental behaviour (verified: the next build was `no work to do`, 0 s).

---

## Remaining levers

| lever | ceiling | note |
|---|---:|---|
| `import storm;` per TU | ~230 s | +1.6 s of the +2.1 s is `storm_orm_queryset`; the umbrella re-exports everything, so a schema-only test pays for the query stack. Touches the public module surface. |
| `test_db_helpers.h` + `shared/models.h` | ~150 s | every TU parses 12 models and generates 49 field-selector proxies to use one. Mechanical to split, but touches 89 files. |

---

## Method

- **Per-TU timing**: replay a TU's exact command from `compile_commands.json`
  with `-o` stripped, serially, min of 3 runs. Timing through `ninja` instead adds
  the scan step and roughly ±2 s of noise; the replay harness holds ±0.1-0.3 s.
- **Always disable ccache when timing.** `CMAKE_CXX_COMPILER_LAUNCHER` set with
  `CACHE ... FORCE` survives removal of the module that set it — `cmake -U
  CMAKE_CXX_COMPILER_LAUNCHER .` clears it. A stale launcher silently served
  objects in 0.2 s and invalidated a whole measurement round.
- **Warm up before measuring.** The first compile in a session is inflated by
  cold page cache for the module BMIs — this produced a fake 47% win once.
- **Whole-build breakdown**: parse `.ninja_log`, deduplicating on
  `(start, end, cmdhash)`.
- **Inside one TU**: `-ftime-trace`, then aggregate `InstantiateFunction` /
  `InstantiateClass` / `Source` events by `args.detail`.
