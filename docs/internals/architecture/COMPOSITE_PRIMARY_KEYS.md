# Composite & Non-Integer Primary Keys

Design notes for primary keys that are not a single DB-generated integer: multi-column
composite keys (`primary_part`, #500-#537) and `storm::UUID` keys (#507, #565). Also covers
the PK/FK column-naming rules those keys forced (#422, #506, #519).

These are implementation notes — the rationale behind each decision and the failure mode it
prevents — kept out of `CLAUDE.md` to keep that file navigable. For user-facing usage see
[FIELD_TYPES.md](../../guide/reference/FIELD_TYPES.md) and
[REFERENTIAL_INTEGRITY.md](../../guide/features/REFERENTIAL_INTEGRITY.md).

## The recurring failure mode

Nearly every bug below is one of two shapes, worth naming once:

**A hardcode that only a non-default model reaches.** A literal `"id"`, a literal `INTEGER`, a
`primary_key_` that assumed one column. Each was correct for every in-tree model, so each shipped
green and stayed dead until a model with a differently-named, differently-typed, or multi-column
key existed. `if constexpr` guards make these invisible: the branch does not instantiate, so it
does not even have to compile.

**SQLite and PostgreSQL disagree about what is an error.** SQLite does not type-check FK targets
and does not validate that a `REFERENCES` column exists; PostgreSQL rejects both outright. So the
same wrong DDL *silently misbehaves* on SQLite and *fails loudly* on PG. SQLite-only testing is
therefore actively misleading here, which is why the tests for these paths are `TYPED_TEST`s
executing on both backends rather than SQL-text assertions alone.

## Composite keys (`primary_part`)

### Annotation and DDL (#500)

`[[= storm::primary_part]]` on two or more members declares a
multi-column PK — a free-standing tag object like `primary` (#492), re-exported to top-level
`storm::`. Deliberately a SEPARATE tag rather than "two `primary` members means composite": reusing
`primary` would silently turn a double-`primary` typo into a legal key. `is_primary_part_member`
detects it; `is_primary_member` now matches it too, so `ModelWithPrimaryKey<T>` accepts composite
models. `BaseStatement` gains `primary_key_members_` (the full PK list in DECLARATION order — the
order the DDL clause emits) and `has_composite_pk_`, alongside the unchanged `primary_key_`/`pk_name_`
(= the first element, so the ~13 files reading them are untouched). `schema.cppm` routes composite
models past the single-PK branch and emits every part as a regular column plus one table-level
`PRIMARY KEY (a, b)`, reusing the junction-table pattern; single-PK DDL is byte-identical.
`needs_index` now excludes every PK member (the table-level key already indexes each part).
New concept `ModelPrimaryKeyValid<T>` (ANDed into the `BaseStatement` constraint list next to
`ModelAnnotationsValid`) rejects: `primary` + `primary_part`, `primary_autoincrement` +
`primary_part`, exactly one `primary_part`, **two or more `primary`/`primary_autoincrement`
members** (the double-`primary` typo — without this the widened machinery would silently accept it
as composite), and a PK annotation on an m2m/reverse-FK container, on a `std::optional<T>`, or
`primary_part` + `unique`. The `std::optional` and relation-container rejections are gated on
`is_primary_member`, so they cover a **single** `primary` too — a deliberate widening (a nullable PK
was accepted before #500 and is never correct: SQLite's legacy NULL quirk admits duplicate keys while
PG rejects them). No in-tree model used that shape. The PK clause writes parts via the canonical column-name writer
(`append_column_name`, #422), so an FK part emits `warehouse_id` — a composite key over FKs is the
canonical association-table case, and naming the bare member would emit a PK over a nonexistent
column. `calculate_column_defs_size` keeps FK columns on the FK-suffix budget for composite models
(the regular suffix under-counts and `ConstexprString` truncates silently); `build_sql_impl` gained a
whole-SQL `std::unreachable()` backstop, since the per-column one can't catch a sizing shortfall. Autoincrement on a composite key is **unrepresentable**,
not just useless — SQLite rejects both spellings at parse time and PG's identity is single-column —
hence the compile-time rejection. **Scope**: annotation + DDL only; UPDATE/DELETE by composite key
land in #501 (below), INSERT/JOIN and composite FKs in #502–#504.
See [FIELD_TYPES.md](../../guide/reference/FIELD_TYPES.md).

### UPDATE and DELETE (#501)

The by-key `WHERE pk = ?` widens to
`WHERE a = ? AND b = ?`, AND-joined in declaration order. Three things changed, each with its own
failure mode. (1) **SQL text**: the clause writer/sizer pair `append_pk_where_clause` /
`pk_where_clause_size` are free functions in `base.cppm` (NOT `BaseStatement` methods — that class is
inherited by every statement type, and `SchemaStatement` already sits at the S1448 ceiling), taking
the PK-member array as a parameter so `erase.cppm` and `update_grammar.cppm` share them without
depending on each other. Parts go through `append_column_name` (#422), so an FK part emits
`warehouse_id`. (2) **Bind arithmetic**: `bind_pk_values` binds all N parts via an index sequence
(each part may be a DIFFERENT type — `int` + `std::string` is the canonical shape, so the dispatch is
per-part at compile time) and threads `param_index` by REFERENCE, which is what removes the old
`++param_index`-per-row stride assumption. In UPDATE the key follows the SET values, so it starts at
N_set+1. (3) **SET-target gate**: `is_settable_member` in BOTH `update_grammar.cppm` and
`upsert_grammar.cppm` now tests `Base::is_pk_member(M)` instead of `M != primary_key_` — the old test
excluded only the FIRST part, leaving `SET b=? WHERE a=? AND b=?` (rewriting the key it matches on)
legal. `bind_field_at_index` gained a **separate** `SkipAllPK` flag rather than widening `SkipPK`:
INSERT passes `SkipPK` too, and composite INSERT is #502 — widening it would silently change INSERT's
bind order as a side effect. Bulk DELETE uses a **row-value IN list**, `(a, b) IN ((?,?),(?,?))`
(PG always, SQLite ≥ 3.15 vs the project's 3.35 floor); the per-column form `a IN (…) AND b IN (…)`
is WRONG — it matches the parts' cross product and deletes unlisted keys. Chunking is now
`MAX_CHUNK_ROWS = 799/N` since each row costs N parameters. The consteval DELETE grammar moved to a
new `erase_grammar.cppm` leaf (mirroring `update_grammar.cppm`, #434) to keep `erase.cppm` under the
file-size limit; its `append_row_placeholder_list` is shared by the consteval max-chunk builder AND
the runtime per-count builder, so the two cannot drift into disagreeing placeholder counts.
Single-PK SQL is byte-identical (regression-asserted), and single-PK UPDATE/DELETE benchmarks are
within noise (≤1.4% deltas at cv 1.1–2.9%).

### INSERT (#502)

A composite key is never DB-generated — `AUTOINCREMENT` (SQLite)
and `GENERATED ... AS IDENTITY` (PG) are single-integer-column features, so every key part is
caller data. That inverts the single-PK rule twice. (1) **The key columns join the INSERT**: the
`SkipPrimaryKey` skip in `FieldNameGrammar::for_each_field_name` and the plain-`SkipPK` branch of
`skips_pk_column` are both gated on `!has_composite_pk_`, so a composite model emits and binds ALL
fields in declaration order (one shared consteval iterator feeds the column list, its sizer, and
the placeholders — they cannot drift). (2) **There is nothing to RETURN**: `insert().execute()` on
a composite model returns `std::expected<void, Error>` with no `RETURNING` emitted, riding the
pre-existing `ReturnId::No` fast path. The plain call resolves there via `default_return_id<T>()`
(consteval: `No` for composite, `Yes` for single-PK), so the call site is IDENTICAL across model
shapes; an explicit `insert<ReturnId::Yes>` on a composite model is a **compile-time error** via
the `ReturnIdSupported<T, R>` concept (unconstrained it would emit `RETURNING <first part>` —
silently lossy), constraining BOTH `QuerySet::insert` and `InsertStatement::query` so the
diagnostic fires at the user's call site. `ReturnId::No` stays valid on every model shape (generic
code that spells it out keeps compiling). The #413 auto-`DEFAULT` caveat was checked and does not
fire: that `DEFAULT` is recovered from a C++ default-member-initializer — a compile-time constant
already in the caller's object, not a DB-generated value. `placeholders_count()` widened to
`field_count_` for composite models (feeds the upsert `auto_update` tail offset — #503's path);
chunking needed NO change (the auto batch size already divides by ALL fields, which becomes exactly
right once the key columns are bound). Single-PK INSERT SQL is byte-identical
(regression-asserted) and the `RETURNING id` path is unchanged. Upsert/`ON CONFLICT` is #503,
JOIN #504.

### FKs and JOINs (#504)

An FK may reference a composite-PK model, and every JOIN
path (regular FK, m2m, reverse-FK) works over multi-column keys. An FK to an N-part target occupies
**N columns**, named `<member>_<part>` (`line_order_id`, `line_product_id`) — the single-column
`<member>_id` spelling is unchanged for single-PK targets, so all pre-#504 SQL is byte-identical
(regression-asserted at every layer). Nullable composite FKs work: `std::optional<Target>` makes all
N columns nullable together.

Four things had to widen in lockstep, each with its own failure mode. (1) **ON clauses** AND-join
one equality per part (`t2.a = t1.fk_a AND t2.b = t1.fk_b`) — matching only the first part would
silently join rows sharing just that part. (2) **The m2m/reverse-FK stitch key** gained `StitchKey`,
a fixed 32-byte inline buffer, for composite owners — a composite key is not one integer and may mix
`int`/`std::string`/`int64_t`; the statement-side extractor and the object-side builder dispatch per
part on the declared type and must stay in agreement. A **single-column PK still keys the map on a
bare `std::uint64_t`**, selected by `if constexpr` on `primary_key_column_count_`: `std::hash<long>`
is the identity function and `operator==` on a `long` is one inlined compare, so routing single-PK
models through `StitchKey` cost ~4% on the m2m eager-load path (`M2MRelation` carries a second
fn-pointer for the narrow extractor; exactly one is populated). (3) **The Q2 `IN`
subquery** uses the row-value form `(a, b) IN (SELECT a, b FROM …)` — the outer comparison side is
parenthesized but the inner SELECT list is **left unwrapped**, because parenthesizing it makes
SQLite parse it as one scalar expression rather than N columns. (4) **Junction DDL** emits one
column per PK part per side, `<Side>_<part>`, with the PRIMARY KEY listing all N+M and each
`FOREIGN KEY` naming the target's real key columns. That last part also fixed a latent bug: the
old `REFERENCES <Side>(id)` named a nonexistent column for a composite side — SQLite silently
accepts an unvalidated FK target, but PG rejects it outright, so composite-PK junction tables could
not be created on PG at all. Composite PK is supported on **either or both** m2m sides.

An FK member used as a `primary_part` stores the *referenced* key's type, not the whole struct —
`pk_part_storage_type` routes through the same `is_fk_field` check as `bind_one_pk_part` (#501).
Without it, a composite key over FKs (the canonical association-table shape) hard-errors inside a
sizer with a message naming neither model nor field. Column names always go through
`append_column_name` (#422), so an FK part emits `warehouse_id`. Sizing is exact: the junction
sizer runs the **writer itself** into a counting sink, so the two cannot drift — and note
`ConstexprString<N>` holds only `N-1` bytes (its `append` stops at `len < N - 1`), so a buffer
sized to exactly the rendered length silently drops the final character.

### Through-model m2m (#536)

`many_to_many_through<Model>` over a composite side
failed with `no such column: t2.<fk>_<part>_id`. Root cause: **two junction tables, two naming
rules, one code path**. The auto-junction is Storm's SYNTHETIC table, so Storm names its columns —
`<Side>_<part>` with each part routed through `append_column_name` (#422), so an FK part gains
`_id`. A through model is a REAL user-declared model whose junction columns are ordinary
composite-FK columns emitted by the regular model DDL (`append_composite_fk_part_column`), which
spells the target part's **bare** identifier: `<member>_<part>`, no `_id`. The rules agree for a
single-column PK and for a composite key of plain columns; they diverge exactly when a PK part is
itself an FK — the canonical association-table shape, which nothing in-tree exercised. `join.cppm`'s
`append_junction_side_col`/`_list` now take a `JunctionNaming` enum (`Auto`/`Through`) derived from
the same `Through` alias that picks the table and column base names, so the three cannot disagree
about which junction is addressed; the single-PK branch stays SHARED (both rules emit exactly
`<side>_id` there), which is what keeps all pre-#536 junction SQL byte-identical.

Fixing the naming alone was not enough — the FK-part shape was unreachable, broken in three more
places that #504 left on the "does not occur in the current fixtures" path, all the same
one-hop-out mistake (an FK **part of the FK target's key** stores the REFERENCED row's key, not the
whole struct — exactly what `bind_one_pk_part` (#501) already handled for T's OWN key):
`bind_one_fk_part` bound the whole struct (a hard `BindableType` error, so this shape did not
compile at all), `extract_one_fk_part` mirrored it, `join.cppm`'s `extract_relation_fk_part` (the
RELATION-side twin, for Q2 result rows — m2m related entities and reverse-FK owners) mirrored it
again, and `append_composite_fk_part_column` typed the column from the part's DECLARED type —
`StorageClass::Fallback` → nullable `TEXT` — so the
table-level `FOREIGN KEY` compared TEXT against the target's `BIGINT` key. That last one is the
#519/#506 asymmetry again: **SQLite accepted it and silently never matched; PG rejected the
`CREATE TABLE`** — so SQLite-only testing was actively misleading, which is why the tests are
TYPED_TEST executing on both backends rather than SQL-text assertions. All four now route through
the existing single-source helpers (`find_fk_primary_key`, `pk_part_storage_type`, moved above its
first use in `schema.cppm`). Every fix is `if constexpr`-dispatched; single-PK and auto-junction SQL
is byte-identical and benchmarks are unchanged.

### At most 4 parts (#537)

A composite key may span at most **4** columns;
a 5th part is a compile-time error via `ModelPrimaryKeyPartLimit<T>` (ANDed into the
`BaseStatement` constraint list next to `ModelPrimaryKeyValid`). The bound comes from
`StitchKey` (#504), the m2m/reverse-FK stitch-map key: a fixed 32-byte inline buffer into which
every part writes exactly one 8-byte word, so 4 parts fill it with **zero slack**. The only prior
guard was `assert(len_ + sizeof(word) <= CAPACITY)` in `append_word`, which `NDEBUG` compiles
out — absent from exactly the Release configuration where the overrun matters, making a 5-part
key a SILENT buffer overrun there. The gate reads `StitchKey::MAX_PARTS` (`= CAPACITY /
sizeof(uint64_t)`), **derived not hardcoded**, so the limit cannot go stale against the buffer.
Growing `CAPACITY` was rejected: #504 measured a wider key costing ~4% on the stitch hot path
(hence single-PK models bypassing `StitchKey` for a bare `uint64_t`); folding surplus parts was
rejected in #504 as a *mis-stitch* (rows attached to the wrong owner), not a recoverable
collision. Gated at the MODEL rather than at the stitch for two reasons: the stitch runs behind
`M2MRelation`'s type-erased fn-pointer vtable where `T` is out of scope (no call site to name the
model in a diagnostic), and gating only models that currently declare a relation would make
adding an unrelated m2m field later fail in a distant file. Single-PK and all in-tree composite
models (2–3 parts) are unaffected — no SQL or hot-path change, so no benchmark movement.

## Key column naming and typing

### PK/FK naming uses the real identifier, not a literal `"id"` (#506)

The single-PK
`CREATE TABLE` branch (`append_single_pk_column_def`) emits `Base::pk_name_` (the PK member's
actual identifier) ahead of the type/constraint suffix, and the per-field FK `REFERENCES
<Related>(<pk>)` clause (`append_fk_column_def`) emits `find_fk_primary_key<FieldType>()`'s
identifier — the same helper `bind_value_by_type` already used for the FK bind splice, so DDL and
DML are guaranteed to name the same column. Previously both hardcoded the literal `"id"`, so a
model whose PK member was not named `id` got DDL and queries that disagreed at runtime ("no such
column"). Composite-PK models are unaffected (routed around this branch, #500). The consteval size
budgets (`column_size_budget`'s `pk_size`, `fk_references_len`) were widened to measure the real
identifier length instead of assuming the fixed 2-char `"id"`; `id`-named models (the ~50 existing
ones) stay byte-identical.

### m2m junction REFERENCES uses the real PK identifier (#519)

The same fix as #506, one path later.
`detail::append_junction_fk`'s single-PK branch emitted the literal `REFERENCES <Side>(id)`, so an
m2m whose owner or related PK member is not named `id` produced junction DDL naming a nonexistent
column — unexecutable `CREATE TABLE`, the #506 failure class. (#504 fixed the COMPOSITE branch and
deliberately left this one: generalising it there would have changed existing junction DDL outside
that issue's scope.) The branch now emits `SideBase::pk_name_` — the same source `join.cppm`'s ON
clauses read, so junction DDL and the two-query eager load cannot drift. Each side resolves
INDEPENDENTLY (owner `id` + related `sticker_id` is a legal shape). The junction's OWN columns stay
`<Side>_id` (`append_junction_side_column_name`): those are the junction's columns, and `join.cppm`
derives them from `table_name_ + "_id"`, never from `pk_name_` — renaming the DDL side alone would
INTRODUCE the very drift this fixes. No sizing change was needed: #504 replaced the old
`5×name + 256` heuristic with a budget measured by rendering the emitter into `ClauseSizer`, so it
tracks the real identifier automatically. `id`-named models keep byte-identical junction DDL
(regression-asserted on the whole string).

### m2m junction column TYPE follows the referenced PK (#565)

The third and last hardcode in
the same function family. `append_junction_side_column_def`'s single-PK branch typed every junction
column `INTEGER`/`BIGINT` unconditionally, so an m2m between `storm::UUID`-PK models (#507) emitted
an integer column whose `REFERENCES <Side>(<uuid_pk>)` clause — correctly NAMED since #519 — pointed
at a `UUID`/`TEXT` key. **PG rejects that `CREATE TABLE` outright** (FK type mismatch, so such a
model could not be created at all); **SQLite accepts it** and stores mismatched affinities silently —
the #536/#519 asymmetry, which is why the tests are TYPED_TEST executing on both backends rather
than SQL-text assertions alone. The branch is now GONE, not fixed: `primary_key_members_[0]` IS
`primary_key_` for a single-PK model, so both shapes route through the one
`pk_part_storage_type` + `storage_class_of` + `sql_type_for` chain the composite branch (#504) and
the per-field FK path (`append_fk_column_def`) already used. An integer PK still renders exactly
`INTEGER NOT NULL`/`BIGINT NOT NULL` — byte-identical, regression-asserted on the whole string.
Each side resolves INDEPENDENTLY, so an integer-PK owner + UUID-PK related is a legal mixed
junction. No sizing change (the #504 `ClauseSizer` budget runs the writer itself).

## UUID primary keys

### The UUID-PK path was dead code until #565

Writing #565's executable test exposed that **the whole UUID-PK path had never run**. #507
shipped as a self-described "partial implementation" whose only UUID-PK model appears in a
`static_assert` — it never generated DDL, never ran a query — so three defects sat behind
`if constexpr` guards that nothing instantiated. All three are fixed here, since #565's DoD
(executable junction DDL + insert through it, on both backends) is unmeetable without them:

1. **UUID-PK DDL never COMPILED**: `SchemaStatement`'s two `uuid_type<D>()` calls
   (`append_single_pk_column_def`, `append_fk_column_def`) were UNQUALIFIED, but that helper lives
   in `namespace detail` and `SchemaStatement` sits outside it. `detail::`-qualified now.
2. **INSERT dropped the key**: `FieldNameGrammar::is_skipped_pk` and
   `BaseStatement::skips_pk_column` skipped any single-column PK as DB-generated, emitting
   `INSERT INTO UuidDoc (title) VALUES (?)` — the caller's key silently discarded, every row
   landing with a NULL id. Both now also exclude `has_uuid_pk_()`, the #502 rule one shape wider:
   `AUTOINCREMENT`/`GENERATED ... AS IDENTITY` are single-INTEGER-column features, so a UUID key is
   always caller data. The two must change TOGETHER — one owns the column list, the other the bind
   sequence, and a disagreement misaligns values against columns.
3. **The UUID bind was wrong twice**: `bind_optional_or_uuid_pk_field` passed a `Statement*` where
   `bind_uuid_pk` takes `StmtType&`, and returned without `++param_index` (every sibling branch
   reaches that through `bind_one`). The missing increment bound the NEXT field over the key at the
   same slot and left the last placeholder unset — surfacing as a NOT NULL violation naming the
   WRONG column.

Still open as **#572** (deliberately out of scope — a BREAKING return-type change): plain
`insert(uuid_model)` defaults to `ReturnId::Yes` and emits `RETURNING <uuid_pk>`, extracted via
`extract_int64` and therefore meaningless. Spell `insert<ReturnId::No>` on a UUID-PK model until
that lands.

