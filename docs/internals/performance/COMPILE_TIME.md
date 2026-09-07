# Compile-Time Analysis

Where Storm's build time actually goes, measured rather than assumed, and what
moves it. All numbers below are measurements on a 4-core dev container
(clang-p2996, `ninja-debug` unless stated), reproducible with the methods in
[Method](#method).

**Summary**: the build is dominated by the ~110 hand-written test TUs, not by
the YAML-driven test corpus. Each test TU used to pay a **~5.8 s fixed floor**
before its first assertion, **entirely frontend** — Backend is 0.03 s. The gtest
PCH removed most of that floor; what survives it is measured in
[After the PCH](#after-the-pch-issue-633).

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

This is the **pre-PCH** floor — the measurement that motivated precompiling
gtest. Probe TUs whose entire body is `int p(){return 0;}` (min of 3 runs each,
warm):

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

  The second of these is an artefact of the empty probe body and does **not**
  survive re-measurement — see [After the PCH](#after-the-pch-issue-633).

## After the PCH (issue #633)

Numbers in this section are from a **different, faster host** than the rest of
the document (gtest alone: 1.65 s here vs 2.2 s above), so read the deltas, not
the absolute seconds.

Precompiling gtest did not only remove gtest's own share of the floor. It
removed most of `import storm;` with it, which is exactly what the causal story
above predicts — once gtest arrives already serialised, there are no textual
libc++ declarations left to reconcile against:

| probe (body `int p(){return 0;}`) | textual gtest | gtest via PCH |
|---|---:|---:|
| gtest | 1.65 | 0.04 |
| + `import std;` | 1.72 | 0.05 |
| + `import storm;` | 3.10 | 0.29 |
| **cost of `import storm;`** | **+1.38** | **+0.24** |

An empty body understates it, because declarations come out of the PCH lazily
and a probe that never mentions `EXPECT_*` never pulls them. With a body that
uses gtest for real, all three variants via the PCH:

| body | no storm | + `import storm;` | delta |
|---|---:|---:|---:|
| `EXPECT_TRUE(true)` | 0.14 | 0.65 | +0.51 |
| strings + vectors | 0.25 | 0.99 | +0.74 |
| `TYPED_TEST` | 0.23 | 1.02 | +0.79 |
| strings + vectors, textual gtest | 1.82 | 3.31 | +1.49 |

So the surviving cost of `import storm;` is **+0.5 to +0.8 s per TU**, scaling
with how much gtest the TU uses — roughly **55-90 s** over the suite, not the
~230 s that issue #633 was opened against.

### What that residual actually is

`-ftime-trace` on `import storm_orm_queryset;` against a gtest-only baseline,
same body, `-ftime-trace-granularity=0`. Of the +0.63 s:

| bucket | delta |
|---|---:|
| `ParseDeclarationOrFunctionDefinition` | +0.29 |
| `PerformPendingInstantiations` | +0.16 |
| `CodeGen Function` + `DebugType` | +0.18 |
| `InstantiateFunction` + `InstantiateClass` | +0.18 |
| `ReadAST` + `Module Load` + `Load External Specializations` | **+0.06-0.10** |

Reading the BMI is under a sixth of it. The rest is **gtest's own
instantiations getting slower**. The heaviest entries with storm imported are,
in order, `testing::internal::CmpHelperEQ`, `EqHelper::Compare`,
`PrintToString`, `UniversalPrint`, and `std::__can_be_converted_to_string_view` /
`std::is_convertible` — gtest and libc++ templates. **No storm declaration
appears anywhere in the top instantiations.**

The mechanism, then, is not that storm is expensive to load. It is that storm's
declarations join the lookup set, and every later template instantiation in the
TU pays for the larger set. That single fact predicts every other result here:
the cost tracks gtest usage rather than storm usage; `storm_orm_queryset`'s own
body costs ~0; putting `import storm;` in the PCH makes things *worse*; and
narrowing what a module exports cannot help, because the cost is in the set
existing, not in what is reachable through it.

It also scales with the size of the declaration set added, not merely with the
act of importing — leaf modules are genuinely free. Over a gtest-only baseline
of 0.24 s, with a real gtest body:

| import | sec | delta |
|---|---:|---:|
| `storm_db_concept`, `storm_db_sqlite`, `storm_orm_generator`, `storm_orm_transaction` | 0.25-0.27 | ~0.00 |
| `storm_orm_where`, `storm_orm_fields`, `storm_orm_statements_orderby` | 0.35-0.41 | +0.10-0.16 |
| `storm_orm_statements_base` | 0.49 | +0.25 |
| `storm_orm_statements_select` | 0.59 | +0.35 |
| **queryset's own imports, without `storm_orm_queryset`** | **0.91** | **+0.67** |
| `storm_orm_queryset` | 0.88 | +0.64 |
| `storm` (umbrella) | 0.95 | +0.71 |

The decisive row is the second-to-last: importing everything
`storm_orm_queryset` imports, but *not* `storm_orm_queryset` itself, costs the
same as importing it. Its own content contributes nothing measurable. The
umbrella adds only +0.07 s on top of it, so even a perfectly narrowed umbrella
is worth ~8 s over the suite.

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

### `import storm;` inside the PCH — a regression, not a win (#633)

The obvious follow-up to precompiling gtest is to precompile the storm import
alongside it, paying the reconciliation once instead of ~110 times. It is
mechanically possible and measurably worse.

CMake does not scan the PCH source for module imports — its
`compile_commands.json` entry carries no `@...modmap` — so the PCH build fails
outright with `fatal error: module 'std' not found`. Appending a real test TU's
modmap by hand gets it to build, and then:

| PCH contents | PCH build | TU (strings) | TU (`TYPED_TEST`) |
|---|---:|---:|---:|
| gtest only (shipping) | 2.15 | **1.00** | **1.01** |
| gtest, then `import storm;` | 8.07 | 1.27 | 1.31 |
| `import storm;`, then gtest | **compile error** | — | — |

Every TU gets **0.27-0.30 s slower**, which follows directly from the mechanism
above: the import does not amortise, it enlarges the lookup set that the PCH's
own contents are then resolved against. Importing storm *before* gtest does not
compile at all — the same textual-libc++-versus-module clash that already forces
four groups of files onto `SKIP_PRECOMPILE_HEADERS` in `tests/CMakeLists.txt`.

Folding the other lever's headers in fails the same way: a PCH of gtest +
`import storm;` + `test_db_helpers.h` + `test_models.h` dies on
`test_db_helpers.h`'s global-module forward declaration of
`storm::db::sqlite::Connection` (`declaration of 'Connection' in the global
module follows declaration in module storm_db_sqlite`). Dropping storm and
precompiling `test_db_helpers.h` alone — the half that needs no storm import —
breaks 3 of 4 sampled TUs inside libc++'s `<format>`
(`call to implicitly-deleted default constructor of 'formatter<...>'`), and the
one TU that still compiles is +0.08 s. So the ~150 s `test_db_helpers.h` lever
is **not** reachable by extending the PCH.

### Narrowing `storm_orm_queryset`'s exports (#633)

Issue #633's second proposed direction. There is nothing to narrow: importing
`storm_orm_queryset`'s dependency list *without* `storm_orm_queryset` costs
+0.67 s against +0.64 s for the module itself. The facade's own content is free;
the cost is the transitive surface a QuerySet consumer needs regardless.

### Tests importing submodules instead of the umbrella (#633)

Issue #633's first proposed direction. It works and it is small.

Of the ~110 test TUs, **9** import `storm` without needing
`storm_orm_queryset` — every other TU reaches `storm::QuerySet` through
`tests/test_models.h`'s fixtures. Three of the nine cannot be converted anyway:
`schema/test_valid_foreign_key_concept.cpp` and `query/test_fields_selector.cpp`
spell `storm::primary`, and `schema/test_dialect_concepts.cpp` calls
`storm::begin` — names that exist only in the umbrella (`storm::primary` is
#442's top-level re-export; converting would mean reverting those tests to
`storm::meta::`).

The remaining six, replayed with their own commands from
`compile_commands.json`:

| TU | `import storm;` | submodules | delta |
|---|---:|---:|---:|
| `schema/test_stitch_key.cpp` | 1.47 | 0.45 | −1.02 |
| `schema/test_valid_field_info_concept.cpp` | 0.96 | 0.37 | −0.60 |
| `schema/test_bindable_concept.cpp` | 3.27 | 1.90 | −1.37 |
| `db/test_statement_move.cpp` | 0.84 | 0.21 | −0.63 |
| `db/test_cache_invalidation.cpp` | 2.44 | 1.21 | −1.23 |
| `db/test_sqlite_tuning.cpp` | 4.98 | 2.94 | −2.04 |
| **total** | **13.97** | **7.08** | **−6.89** |

−6.9 s of ~3000 s (0.2%), in exchange for six test files naming internal module
names. Not adopted. `errors/test_error_construction.cpp` and
`yaml/test_init_dataset_size.cpp` already import submodules directly, so the
option remains open per-file if a TU ever needs it.

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
| precompiled `<gtest/gtest.h>` for `storm_tests` | −2.15 s/TU ≈ **−220 s** (understated — see below) |
| `run_clang_tidy.sh --diff` no longer reports a crashed clang-tidy as a clean pass | correctness |

≈ **−280 s of ~3000 s (−9%)**.

The gtest PCH row understates what it bought. It was measured as the removal of
gtest's own share of the floor, but it also took most of `import storm;` with it
— +1.38 s per TU down to +0.24-0.79 s, roughly another 100 s — because the
reconciliation it removes is between storm's BMI and gtest's *textual* libc++
declarations. See [After the PCH](#after-the-pch-issue-633).

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
| ~~`import storm;` per TU~~ | ~~230 s~~ | **Closed by measurement (#633).** The gtest PCH already took it from +1.38 s to +0.5-0.8 s per TU. What remains is gtest's own instantiations slowing down because storm's declarations are in the lookup set — not storm being loaded — so it is not reachable from storm's side. Both directions #633 proposed were measured and rejected above; together they are worth ~15 s. |
| `test_db_helpers.h` + `shared/models.h` | ~150 s | every TU parses 12 models and generates 49 field-selector proxies to use one. Mechanical to split, but touches 89 files. **Not** reachable by adding these headers to the PCH — that was measured and fails (see above). |

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
