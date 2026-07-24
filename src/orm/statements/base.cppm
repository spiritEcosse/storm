module;

// Single cohesive class template; thresholds intentionally relaxed (see #264 finding).
// `duplicate` removed in #277 Phase 3 (bind_bulk_objects_impl + bind_expr_or_reset helpers; the
// for_each_field_name dedup now lives in storm_orm_statements_field_names, #434).

#include <meta>

export module storm_orm_statements_base;

import std;

import storm_db_concept;
import storm_orm_field_attr;
import storm_orm_relation_meta;
import storm_orm_utilities;
import storm_orm_statements_extract;
import storm_orm_statements_field_names;
import storm_orm_statements_orderby;
import storm_orm_where;

export namespace storm::orm::statements {

    // Import utilities for compile-time string building
    using storm::orm::utilities::ConstexprString;

    namespace meta {
        // Canonical flag annotations + is_primary_member live in the dependency-free
        // storm_orm_field_attr leaf module (#387, #492); re-exposed here so statement
        // modules keep using the meta:: qualifier.
        using storm::meta::is_primary_member;
        using storm::meta::is_primary_part_member; // NOLINT(misc-unused-using-decls) — #500 composite PK
        using storm::meta::ref_action_sql;         // NOLINT(misc-unused-using-decls) — used by storm_orm_schema
        using storm::meta::RefAction;

        // Per-attribute predicates (#421) re-exposed from the leaf so statement modules
        // keep the meta:: qualifier.
        using storm::meta::has_full_unsigned_attr;  // NOLINT(misc-unused-using-decls)
        using storm::meta::has_signed_storage_attr; // NOLINT(misc-unused-using-decls)
        using storm::meta::is_auto_create;
        using storm::meta::is_auto_update;
        using storm::meta::is_indexed;
        using storm::meta::is_unique;

        // Many-to-many (#203, #431) and reverse-FK (#398) annotation TYPES and their
        // "is this a relation member, not a persisted column?" detection predicates live
        // in the storm_orm_relation_meta leaf module (#408) so storm_orm_where can gate
        // f<>() without importing this module. Re-exposed here so statement modules keep
        // the meta:: qualifier (mirrors how FK detection is re-exposed from field_attr).
        using storm::meta::is_m2m_field;
        using storm::meta::is_relation_field; // NOLINT(misc-unused-using-decls) — #408 chokepoint
        using storm::meta::is_reverse_fk_field;
        using storm::meta::m2m_annotation_type_of;
        using storm::meta::many_to_many;         // NOLINT(misc-unused-using-decls) — model-declaration spelling
        using storm::meta::many_to_many_through; // NOLINT(misc-unused-using-decls) — model-declaration spelling
        using storm::meta::ManyToMany;           // NOLINT(misc-unused-using-decls) — re-exported for storm.cppm
        using storm::meta::reverse_fk;           // NOLINT(misc-unused-using-decls) — model-declaration spelling
        using storm::meta::reverse_fk_annotation_type_of;
        using storm::meta::ReverseFk; // NOLINT(misc-unused-using-decls) — re-exported for storm.cppm

        // Foreign-key annotation (#431) lives in the storm_orm_field_attr leaf module so
        // every statement module can detect FK fields without importing this one. Re-exposed
        // here so statement modules keep using the meta:: qualifier.
        using storm::meta::
                append_column_name; // NOLINT(misc-unused-using-decls) — #422 canonical <identifier>[_id] writer
        using storm::meta::column_name_size; // NOLINT(misc-unused-using-decls) — #422 its byte-exact size companion
        using storm::meta::fk;               // NOLINT(misc-unused-using-decls)
        using storm::meta::Fk;               // NOLINT(misc-unused-using-decls)
        using storm::meta::fk_annotation_type_of; // NOLINT(misc-unused-using-decls)
        using storm::meta::fk_on_delete_action_of;
        using storm::meta::is_fk_field;

        // Text-length bound annotation (#493) — lives in the storm_orm_field_attr leaf
        // module so schema.cppm can emit VARCHAR(N)/CHECK without importing this one.
        using storm::meta::is_text_member; // NOLINT(misc-unused-using-decls)
        using storm::meta::max_length;     // NOLINT(misc-unused-using-decls) — model-declaration spelling
        using storm::meta::max_length_of;  // NOLINT(misc-unused-using-decls)
        using storm::meta::MaxLength;      // NOLINT(misc-unused-using-decls) — re-exported for storm.cppm

        // True iff `type` has at least one non-static data member annotated with
        // storm::primary/primary_autoincrement/primary_part (#500 — a composite key
        // satisfies this like any other). The info-value core of the ModelWithPrimaryKey<T> concept
        // (below), factored out so it can also run on an info VALUE — needed by
        // valid_fk_target, whose target type is derived from a range-for loop variable
        // over nonstatic_data_members_of and so cannot be spliced into a type template
        // argument (splice operands must be constant expressions; a plain for-loop
        // variable over a heap-backed std::vector<info> range is not one).
        consteval auto has_primary_key(std::meta::info type) -> bool {
            for (auto m : std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())) {
                if (meta::is_primary_member(m)) {
                    return true;
                }
            }
            return false;
        }

        // std::optional<T> → T (dealiased); any other type returned dealiased unchanged.
        // Shared optional-unwrap for FK-target queries.
        consteval auto unwrap_optional_type(std::meta::info type) -> std::meta::info {
            auto t = std::meta::dealias(type);
            if (std::meta::has_template_arguments(t) &&
                std::meta::template_of(t) == std::meta::template_of(std::meta::dealias(^^std::optional<int>))) {
                return std::meta::dealias(std::meta::template_arguments_of(t)[0]);
            }
            return t;
        }

        // True iff the FK field type `fk_type` refers to an entity that has a primary key.
        // The info-value equivalent of the ValidForeignKey<FieldType> concept: unwrap an
        // optional<Related> to Related structurally (same pattern as fk_member_points_at),
        // then delegate to has_primary_key. Used by FKFieldOf (#474), which cannot splice
        // its range-for loop variable into ValidForeignKey<typename[:...:]> directly (see
        // has_primary_key above). Single-level: only the target's own PK, never recursing
        // into the target's FKs. Intentionally parallel to ValidForeignKey<FieldType>: the
        // concept serves the type-argument path (find_fk_primary_key), this the loop-variable
        // path (FKFieldOf); both compute "the FK target, optional-unwrapped, has a PK".
        consteval auto valid_fk_target(std::meta::info fk_type) -> bool {
            return has_primary_key(unwrap_optional_type(fk_type));
        }

        // The raw template argument of a reverse_fk member's annotation — either an
        // owner type (^^Task) or an FK field (^^Task::assignee). The join machinery
        // resolves it to the concrete FK field via resolve_reverse_fk_target.
        // Precondition: `member` is a reverse_fk field (callers gate on
        // is_reverse_fk_field), so the annotation is always present.
        consteval auto reverse_fk_target_of(std::meta::info member) -> std::meta::info {
            const auto annotation_type =
                    reverse_fk_annotation_type_of(member).value(); // NOLINT(bugprone-unchecked-optional-access)
            return std::meta::extract<std::meta::info>(std::meta::template_arguments_of(annotation_type)[0]);
        }

        // True when the FK member `fk_member` (an fk<...> data member) points back
        // at base_t — its declared type, optional-unwrapped, is exactly base_t. The
        // single "does this FK reverse to the base?" check across the reverse-FK code.
        consteval auto fk_member_points_at(std::meta::info fk_member, std::meta::info base_t) -> bool {
            return unwrap_optional_type(std::meta::type_of(fk_member)) == base_t;
        }

        // Count of fk<...> members of `owner` whose type points back at base_t.
        consteval auto count_fks_to(std::meta::info owner, std::meta::info base_t) -> std::size_t {
            std::size_t count = 0;
            for (auto m : std::meta::nonstatic_data_members_of(owner, std::meta::access_context::unchecked())) {
                if (is_fk_field(m) && fk_member_points_at(m, base_t)) {
                    ++count;
                }
            }
            return count;
        }

        // Validate a reverse_fk member's target against base_t (the model owning the
        // container). A type target requires the owner to expose exactly ONE FK back at
        // base_t; a field target must be an fk<...> member of another model that
        // points at base_t. Precondition: `member` is a reverse_fk field.
        consteval auto reverse_fk_member_valid(std::meta::info member, std::meta::info base_t) -> bool {
            const auto target = reverse_fk_target_of(member);
            if (std::meta::is_type(target)) {
                return count_fks_to(target, base_t) == 1;
            }
            if (!std::meta::is_nonstatic_data_member(target) || std::meta::parent_of(target) == base_t) {
                return false;
            }
            return is_fk_field(target) && fk_member_points_at(target, base_t);
        }

        // Resolve a reverse_fk target (owner type OR FK field) to the concrete FK field
        // pointing back at base_t. A type target picks the unique fk<...> member
        // whose (optional-unwrapped) type is base_t; a field target is used directly.
        // Takes both as runtime consteval args so it works on loop-variable members.
        consteval auto resolve_reverse_fk_target(std::meta::info target, std::meta::info base_t) -> std::meta::info {
            if (std::meta::is_type(target)) {
                for (auto m : std::meta::nonstatic_data_members_of(target, std::meta::access_context::unchecked())) {
                    if (is_fk_field(m) && fk_member_points_at(m, base_t)) {
                        return m;
                    }
                }
                std::unreachable(); // ReverseFKFieldOf guarantees a unique FK exists
            }
            return target; // already an FK field
        }

        // True for m2m members WITHOUT a through model (auto-generated junction table).
        consteval auto is_m2m_auto(std::meta::info member) -> bool {
            auto type = m2m_annotation_type_of(member);
            return type.has_value() && std::meta::dealias(std::meta::template_arguments_of(type.value())[0]) == ^^void;
        }

        // Junction ON DELETE RefAction of an auto-junction m2m member (#431): template arg
        // [1] of its ManyToMany annotation, applied to BOTH junction FK sides. Defaults to
        // CASCADE. Precondition: `member` is an m2m field (callers gate on is_m2m_field).
        consteval auto m2m_junction_on_delete_of(std::meta::info member) -> RefAction {
            const auto type = m2m_annotation_type_of(member).value(); // NOLINT(bugprone-unchecked-optional-access)
            return std::meta::extract<RefAction>(std::meta::template_arguments_of(type)[1]);
        }

        // Through model of an m2m member (void = auto-generated junction table).
        template <std::meta::info Member>
        using m2m_through_t = typename[:std::meta::template_arguments_of(m2m_annotation_type_of(Member).value())[0]:];

        // Related model type extracted from a container field type via C++26 std::meta (#203):
        // vector<Course> → Course, plf::hive<Track> → Track,
        // vector<shared_ptr<Course>> / vector<unique_ptr<Course>> → Course.
        consteval auto related_type_from_container(std::meta::info container_type) -> std::meta::info {
            const auto first =
                    std::meta::dealias(std::meta::template_arguments_of(std::meta::dealias(container_type))[0]);
            if (std::meta::has_template_arguments(first)) {
                // ^^std::shared_ptr names a using-declarator under `import std;` and can't
                // be reflected directly — derive the canonical template from a concrete
                // specialization instead.
                const auto tmpl            = std::meta::template_of(first);
                const auto shared_ptr_tmpl = std::meta::template_of(std::meta::dealias(^^std::shared_ptr<int>));
                const auto unique_ptr_tmpl = std::meta::template_of(std::meta::dealias(^^std::unique_ptr<int>));
                if (tmpl == shared_ptr_tmpl || tmpl == unique_ptr_tmpl) {
                    return std::meta::dealias(std::meta::template_arguments_of(first)[0]);
                }
            }
            return first;
        }

        template <typename Container> using m2m_related_t = typename[:related_type_from_container(^^Container):];

        // Detect std::shared_ptr container elements (m2m append path wraps the
        // extracted related object in make_shared, #203).
        template <typename T> struct is_shared_ptr : std::false_type {};
        template <typename TValue> struct is_shared_ptr<std::shared_ptr<TValue>> : std::true_type {};
        template <typename TValue> constexpr bool is_shared_ptr_v = is_shared_ptr<TValue>::value;
    } // namespace meta

    // Concept: T must have at least one field annotated with storm::primary,
    // storm::primary_autoincrement, or storm::primary_part (#492, #500). A composite key
    // (two or more primary_part members) satisfies it exactly like a single-column one;
    // whether the PK DECLARATION is coherent is the separate ModelPrimaryKeyValid<T>.
    //
    // Because a primary key is itself a non-static data member, satisfying this concept
    // also guarantees `field_count_ >= 1`. That invariant is what makes the INSERT batch
    // divides `MAX_DB_VARIABLES / field_count_` (insert.cppm) safe from division by zero —
    // the divisor can never be 0 for any T that reaches a statement class (issue #362, item A).
    template <typename T>
    concept ModelWithPrimaryKey = meta::has_primary_key(^^T);

    namespace meta {
        // True iff `member`'s (dealiased) type is a signed integral type admitted as a
        // primary key (#505) — DECIDED 2026-07-22: short/int/long/long long and their
        // fixed-width spellings (std::int16_t, std::int32_t, std::int64_t, ...); NOT
        // char/signed char (a 1-byte identity is pathological) or bool (integral, but
        // a two-valued PK is nonsense — same carve-out NumericAggregateable makes,
        // #475). std::optional<T> is rejected outright — a nullable primary key is
        // meaningless, so unlike is_unsigned64_member this does NOT unwrap optional
        // first. A bare 64-bit unsigned type is rejected unless
        // annotated storm::signed_storage: that annotation routes through the same
        // bind_int64/extract_int64 path every hardcoded-int64 PK site already uses
        // (select.cppm, join.cppm, insert.cppm); storm::full_unsigned stores as
        // zero-padded TEXT and would silently misread through those same sites, so it
        // stays rejected even though it is an explicit storage annotation.
        consteval auto is_primary_key_typed_member(std::meta::info member) -> bool {
            const std::meta::info t = std::meta::dealias(std::meta::type_of(member));
            if (t == ^^bool) {
                return false;
            }
            if (t == ^^short || t == ^^int || t == ^^long || t == ^^long long) {
                return true;
            }
            if (t == ^^unsigned long || t == ^^unsigned long long) {
                return meta::has_signed_storage_attr(member);
            }
            if (t == ^^storm::orm::utilities::UUID) {
                return true;
            }
            return false;
        }
    } // namespace meta

    // Concept: T's SINGLE primary-key member (storm::primary / primary_autoincrement) has
    // a type admitted as a primary key (#505). Scoped to the single-column case only —
    // a composite key (two or more storm::primary_part members, #500) is exempted
    // entirely and returns true unconditionally, for two reasons: (1) issue #505 was
    // decided before composite PKs existed in this codebase, so the accepted-type set
    // was never scoped against composite parts; (2) an FK-typed composite part (e.g.
    // StockEntry::warehouse in tests/crud/test_composite_pk_models.h) binds via the
    // REFERENCED row's key (meta::is_fk_field routing in bind_one_pk_part), not its own
    // declared type, so "the member's type" is not even the right question for that part
    // — and a plain non-FK TEXT part (e.g. Inventory::sku, same file) is an already-shipped,
    // already-tested composite-key shape. Composite-PK type constraints are their own
    // unscoped follow-up, not a silent widening of this concept's contract.
    //
    // ANDed alongside ModelWithPrimaryKey on BaseStatement's constraint list — a model
    // with NO primary key already fails that sibling concept, so the loop here always
    // finds a match when both concepts are evaluated together on a single-PK model.
    // Checked directly on the member from nonstatic_data_members_of(^^T) (BMI-safe,
    // #262), matching the sibling model-policy concepts (ModelStorageAnnotated,
    // ModelFkPoliciesValid, ...).
    //
    // A failing BaseStatement<T> instantiation names both T and PrimaryKeyType in the
    // "constraints not satisfied" / "because 'T' does not satisfy 'PrimaryKeyType'"
    // diagnostic trail (matching every sibling concept here — none of them name the
    // specific member either). A static_assert with a dynamic message naming the PK
    // member was tried and rejected: static_assert failure inside a concept's consteval
    // lambda is a hard compile error during constraint checking, not a SFINAE-friendly
    // false, so it would break every negative `!PrimaryKeyType<Bad>` usage (including
    // the tests) instead of merely gating BaseStatement.
    template <typename T>
    concept PrimaryKeyType = []() consteval {
        for (auto m : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
            if (meta::is_primary_part_member(m)) {
                return true; // composite key: type constraint out of scope for #505
            }
            if (meta::is_primary_member(m)) {
                return meta::is_primary_key_typed_member(m);
            }
        }
        return true; // no PK member: ModelWithPrimaryKey<T> is the concept that rejects this
    }();

    // A field type is a valid FK target iff its referenced entity (optional-unwrapped)
    // has a primary key. Names the boundary find_fk_primary_key relies on and that
    // FKFieldOf did not previously enforce at the call site (#474). Single-level: it
    // checks only the target's PK, never recursing into the target's own FKs — so a
    // self-referential (an FK field whose target is its own owning model) or
    // mutually-referential model terminates in one step.
    template <typename FieldType>
    concept ValidForeignKey = ModelWithPrimaryKey<utilities::optional_inner_type_t<FieldType>>;

    // Concept: every 64-bit unsigned field of T must carry an explicit storage
    // annotation — storm::signed_storage or storm::full_unsigned (#436). A bare
    // unsigned-64 field would silently store > INT64_MAX values as a negative int64
    // (equality round-trips, but ORDER BY and external readers see the signed value),
    // so we refuse it at the call site instead. Signed-64 and all smaller types are
    // exempt. optional<uint64_t> is checked on its inner type. The annotation is read
    // directly on members from nonstatic_data_members_of(^^T) (the ModelWithPrimaryKey
    // pattern), which is BMI-safe (#262).
    template <typename T>
    concept ModelStorageAnnotated = []() consteval {
        for (auto m : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
            if (storm::meta::is_unsigned64_member(m) &&
                !(meta::has_signed_storage_attr(m) || meta::has_full_unsigned_attr(m))) {
                return false;
            }
        }
        return true;
    }();

    // Concept: every FK field carrying fk<RefAction::SetNull> must be a nullable FK —
    // std::optional<Related> (#431). ON DELETE SET NULL writes NULL into the child column,
    // which a NOT NULL FK column cannot hold, so we refuse it at the call site with a clear
    // constraint violation instead of letting the database reject the DELETE at runtime.
    // The annotation is read directly on members from nonstatic_data_members_of (BMI-safe,
    // #262); optional-ness is a structural query on the member's type.
    template <typename T>
    concept ModelFkPoliciesValid = []() consteval {
        for (auto m : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
            auto action = meta::fk_on_delete_action_of(m);
            if (action.has_value() && action.value() == meta::RefAction::SetNull) {
                const auto type = std::meta::dealias(std::meta::type_of(m));
                const bool is_optional =
                        std::meta::has_template_arguments(type) && std::meta::template_of(type) == ^^std::optional;
                if (!is_optional) {
                    return false;
                }
            }
        }
        return true;
    }();

    // Concept: no member of T carries a conflicting pair of flag annotations (#492).
    // The former `enum class FieldAttr` made these conflicts unrepresentable — a field
    // could carry at most one enumerator. The free-standing annotation objects can be
    // stacked, so this concept restores the exclusivity guarantee at compile time,
    // rejecting per member:
    //   * both primary and primary_autoincrement (two primary-key modes),
    //   * both signed_storage and full_unsigned (two 64-bit storage modes).
    // Scope is exactly those two mutually-exclusive mode pairs; other flag properties
    // are validated by the sibling concepts (storage-type presence by ModelStorageAnnotated,
    // FK-policy nullability by ModelFkPoliciesValid, timestamp field type by the
    // ValidTimestampField static_assert). Members are read directly from
    // nonstatic_data_members_of(^^T) (BMI-safe, #262), matching those siblings.
    template <typename T>
    concept ModelAnnotationsValid = std::ranges::all_of(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()),
            [](std::meta::info m) consteval {
                const bool primary_conflict = storm::meta::has_annotation_type<storm::meta::Primary>(m) &&
                                              storm::meta::has_annotation_type<storm::meta::PrimaryAutoincrement>(m);
                const bool storage_conflict = meta::has_signed_storage_attr(m) && meta::has_full_unsigned_attr(m);
                return !primary_conflict && !storage_conflict;
            }
    );

    // Counts of each primary-key annotation flavour on T (#500), computed in one pass
    // so ModelPrimaryKeyValid can express its rules as plain arithmetic on the result.
    // A struct rather than a pair: the three counts are read by name at every use.
    struct PrimaryKeyCounts {
        std::size_t primary{};       // storm::primary
        std::size_t autoincrement{}; // storm::primary_autoincrement
        std::size_t part{};          // storm::primary_part (composite)
        std::size_t on_relation{};   // any PK annotation sitting on an m2m/reverse_fk member
        std::size_t nullable{};      // any PK annotation sitting on a std::optional<T> member
        std::size_t unique_part{};   // a primary_part member that also carries storm::unique
    };

    template <typename T> consteval auto count_primary_key_flavours() -> PrimaryKeyCounts {
        PrimaryKeyCounts counts;
        for (auto m : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
            counts.primary += static_cast<std::size_t>(storm::meta::has_annotation_type<storm::meta::Primary>(m));
            counts.autoincrement +=
                    static_cast<std::size_t>(storm::meta::has_annotation_type<storm::meta::PrimaryAutoincrement>(m));
            counts.part += static_cast<std::size_t>(storm::meta::is_primary_part_member(m));
            if (storm::meta::is_primary_member(m)) {
                // A PK on a relation container names something that is never a column,
                // and a nullable PK column is not a key at all (SQLite's legacy NULL
                // quirk even lets duplicate keys through, diverging from PG on identical
                // DDL). Counted here so ModelPrimaryKeyValid can refuse both.
                counts.on_relation += static_cast<std::size_t>(storm::meta::is_relation_field(m));
                // UNIQUE on one part of a composite key defeats the key: it allows at
                // most one row per that part alone, making the other parts pointless.
                counts.unique_part +=
                        static_cast<std::size_t>(storm::meta::is_primary_part_member(m) && storm::meta::is_unique(m));
                counts.nullable += static_cast<std::size_t>(
                        std::meta::has_template_arguments(std::meta::dealias(std::meta::type_of(m))) &&
                        std::meta::template_of(std::meta::dealias(std::meta::type_of(m))) == ^^std::optional
                );
            }
        }
        return counts;
    }

    // Concept: T's primary-key declaration is coherent (#500). Composite support widens
    // the PK from "exactly one member" to "one or more", which makes three new
    // combinations expressible that must not be:
    //
    //   * primary + primary_part — two competing PK declarations; which one is the key?
    //   * primary_autoincrement + primary_part — additionally UNREPRESENTABLE in SQL.
    //     SQLite rejects both spellings at parse time: a column-level PRIMARY KEY cannot
    //     coexist with a table-level PRIMARY KEY (...), and AUTOINCREMENT is only
    //     grammatical directly after INTEGER PRIMARY KEY. PG is equivalent — GENERATED
    //     AS IDENTITY is single-column, there is no multi-column identity. Because it
    //     cannot be spelled at all, emitting the DDL and letting the backend object
    //     would surface a runtime error about primary keys that never names the
    //     annotation responsible; rejecting here gives a constraint violation at the
    //     model definition instead.
    //   * exactly one primary_part — that is a plain PK written the wrong way; tell the
    //     user to spell it `primary` rather than silently accepting a 1-column
    //     "composite" key that emits different DDL than the equivalent `primary`.
    //   * two or more `primary` / `primary_autoincrement` members — the accidental
    //     double-`primary` typo the separate primary_part tag exists to keep an error.
    //   * a PK annotation on an m2m/reverse_fk container, or on a std::optional<T>, or
    //     `unique` on a single part — none of which is a well-formed key column.
    //
    // Consequence carried into #502: since no part of a composite PK can be DB-generated,
    // the caller always supplies the full key — those columns are always INSERTed and
    // there is nothing to RETURNING.
    //
    // Zero primary_part members is the single-PK case, unconstrained here and validated
    // by ModelWithPrimaryKey as before. Sits next to ModelAnnotationsValid in the
    // BaseStatement constraint list; that concept keeps rejecting per-member conflicts
    // (primary + primary_autoincrement on ONE member), this one whole-model PK coherence.
    template <typename T>
    concept ModelPrimaryKeyValid = []() consteval {
        constexpr auto counts = count_primary_key_flavours<T>();
        // A PK annotation on a member that is not a plain persisted column: an m2m /
        // reverse_fk container (never emitted as a column, so the key would reference a
        // name no column definition provides — the #485 class of leak) or a nullable
        // std::optional<T> (a nullable PK is not a key, and SQLite vs PG disagree on it).
        if (counts.on_relation > 0 || counts.nullable > 0) {
            return false;
        }
        // UNIQUE on a single part contradicts the composite key it belongs to (see
        // count_primary_key_flavours). Valid SQL, but never what the author meant.
        if (counts.unique_part > 0) {
            return false;
        }
        // Several column-level PK declarations: `primary` (or primary_autoincrement) on
        // two different members. This is the accidental-double-`primary` typo the separate
        // primary_part tag exists to keep an ERROR — without this clause the widened PK
        // machinery would happily treat it as a composite key and silently emit different
        // DDL than the same model produced before composite support existed.
        if (counts.primary + counts.autoincrement > 1) {
            return false;
        }
        if (counts.part == 0) {
            return true; // single-PK model — unchanged rules
        }
        if (counts.primary > 0 || counts.autoincrement > 0) {
            return false; // primary_part mixed with a column-level PK declaration
        }
        return counts.part >= 2; // exactly one part is a plain `primary`
    }();

    // Concept: every max_length<N> annotation of T must sit on a text field (#493).
    // max_length bounds a text column's length (VARCHAR(N) on PG, CHECK(length) on SQLite);
    // on a non-text field it is meaningless, so we refuse it at the call site with a clear
    // constraint violation instead of silently ignoring it (unlike Django/SQLAlchemy). Only
    // std::string / std::string_view (optional-unwrapped) accept it. Members are read
    // directly from nonstatic_data_members_of(^^T) (BMI-safe, #262), matching the sibling
    // concepts above.
    template <typename T>
    concept ModelMaxLengthValid = std::ranges::all_of(
            std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()),
            [](std::meta::info m) consteval { return !meta::max_length_of(m).has_value() || meta::is_text_member(m); }
    );

    // A field carrying auto_create/auto_update must be a system_clock::time_point (#209).
    // Referenced by a static_assert in bind_field_at_index so a wrong-typed timestamp field
    // fails to compile with a clear message rather than deep inside parameter binding.
    template <std::meta::info Member>
    concept ValidTimestampField = std::
            same_as<std::remove_cvref_t<typename[:std::meta::type_of(Member):]>, std::chrono::system_clock::time_point>;

    // A JOIN field selector must reflect a non-static data member of T annotated with
    // an fk<...> annotation (#388). Constrains QuerySet::join/left_join and
    // JoinStatement so a non-member or non-FK argument fails at the call site with a
    // clear constraint violation.
    //
    // The annotation is read from the member re-derived out of ^^T (matched by
    // identifier), NOT from Member itself: annotation_of_type on a reflection that
    // crossed a BMI boundary segfaults clang-p2996 (#262), while structural queries
    // (is_nonstatic_data_member / parent_of / identifier_of) are safe on it.
    template <typename T, std::meta::info Member>
    concept FKFieldOf = []() consteval {
        if (!std::meta::is_nonstatic_data_member(Member) || !std::meta::has_identifier(Member)) {
            return false;
        }
        if (std::meta::parent_of(Member) != ^^T) {
            return false;
        }
        for (auto m : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
            if (std::meta::identifier_of(m) == std::meta::identifier_of(Member)) {
                return meta::is_fk_field(m) && meta::valid_fk_target(std::meta::type_of(m));
            }
        }
        return false;
    }();

    // A many-to-many JOIN field selector must reflect a non-static data member of T
    // carrying a ManyToMany annotation (#203). Same BMI-boundary discipline as
    // FKFieldOf: the annotation is read from the member re-derived out of ^^T.
    template <typename T, std::meta::info Member>
    concept M2MFieldOf = []() consteval {
        if (!std::meta::is_nonstatic_data_member(Member) || !std::meta::has_identifier(Member)) {
            return false;
        }
        if (std::meta::parent_of(Member) != ^^T) {
            return false;
        }
        for (auto m : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
            if (std::meta::identifier_of(m) == std::meta::identifier_of(Member)) {
                return meta::is_m2m_field(m);
            }
        }
        return false;
    }();

    // A cross-model reverse-FK selector (#398): Member is an fk<...> data member
    // of ANOTHER model whose FK type (unwrapping std::optional) is the base model T.
    //   QuerySet<Person>().left_join<^^Task::assignee>()  // Member = ^^Task::assignee
    // The owning model (Task), FK field, and FK target (Person) all come from Member;
    // naming the field disambiguates several FKs to the same target (assignee vs reviewer).
    // Structural-only queries on Member (parent_of/type_of/identifier_of) — safe across a
    // BMI boundary; the annotation is read from the member re-derived out of the owner.
    template <typename T, std::meta::info Member>
    concept ReverseFKSelector = []() consteval {
        if (!std::meta::is_nonstatic_data_member(Member) || !std::meta::has_identifier(Member)) {
            return false;
        }
        const auto owner = std::meta::parent_of(Member);
        if (owner == ^^T) {
            return false; // a reverse FK must point from a DIFFERENT model back at T
        }
        // The FK type, optional-unwrapped, must be exactly T.
        if (!meta::fk_member_points_at(Member, ^^T)) {
            return false;
        }
        // Member must carry an fk<...> annotation — read from the member re-derived out of owner.
        for (auto m : std::meta::nonstatic_data_members_of(owner, std::meta::access_context::unchecked())) {
            if (std::meta::identifier_of(m) == std::meta::identifier_of(Member)) {
                return meta::is_fk_field(m);
            }
        }
        return false;
    }();

    // A reverse-FK container destination (#398): a non-static data member of T carrying a
    // ReverseFk annotation, whose carried FK field is itself a valid ReverseFKSelector<T>.
    // This is the key+destination form (select()); ReverseFKSelector is the key alone
    // (aggregate/filter chains). Same BMI discipline: annotation re-derived from ^^T.
    template <typename T, std::meta::info Member>
    concept ReverseFKFieldOf = []() consteval {
        if (!std::meta::is_nonstatic_data_member(Member) || !std::meta::has_identifier(Member)) {
            return false;
        }
        if (std::meta::parent_of(Member) != ^^T) {
            return false;
        }
        for (auto m : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
            if (std::meta::identifier_of(m) == std::meta::identifier_of(Member)) {
                return meta::is_reverse_fk_field(m) && meta::reverse_fk_member_valid(m, ^^T);
            }
        }
        return false;
    }();

    // A through model must carry exactly one fk<...> member of type Side
    // (#203 Phase 2) — that member names the junction FK column (<identifier>_id).
    template <typename Through, typename Side>
    concept ThroughWithFKTo = []() consteval {
        std::size_t count = 0;
        for (auto m : std::meta::nonstatic_data_members_of(^^Through, std::meta::access_context::unchecked())) {
            if (meta::is_fk_field(m) && std::meta::dealias(std::meta::type_of(m)) == std::meta::dealias(^^Side)) {
                ++count;
            }
        }
        return count == 1;
    }();

    // Parse a decimal string to uint64 for full_unsigned extraction (#436); leading
    // zeros from the SQLite zero-padded TEXT (or PG NUMERIC) are ignored. Defensive 0
    // on an unexpected empty/garbage value — the column is NOT NULL decimal by
    // construction. Free function (no model state) so it stays off BaseStatement's
    // method count (cpp:S1448).
    inline auto parse_full_unsigned(std::string_view text) noexcept -> std::uint64_t {
        std::uint64_t value = 0;
        std::from_chars(text.data(), text.data() + text.size(), value);
        return value;
    }

    // Extract a full_unsigned column (#436) into obj.[:Member:]: parse the decimal text
    // (SQLite zero-padded TEXT or PG NUMERIC) back to uint64; nullopt on a NULL optional.
    // Free function template (Member NTTP) so it stays off BaseStatement's method count
    // (cpp:S1448) and keeps extract_column_fast's nesting under the limit (cpp:S134).
    template <std::meta::info Member, typename FieldType, typename Obj, typename Statement>
    __attribute__((always_inline)) void extract_full_unsigned_into(Statement* stmt, Obj& obj, int col_idx) noexcept {
        if constexpr (utilities::is_optional_v<FieldType>) {
            obj.[:Member:] = stmt->is_null(col_idx)
                                     ? std::optional<std::uint64_t>(std::nullopt)
                                     : parse_full_unsigned(ColumnExtractor::read_text_view(stmt, col_idx));
        } else {
            obj.[:Member:] = parse_full_unsigned(ColumnExtractor::read_text_view(stmt, col_idx));
        }
    }

    // ── Primary-key WHERE clause (#501) ──────────────────────────────────────────
    // The by-key WHERE that UPDATE-by-object and DELETE-by-object share. For a single
    // PK this emits exactly the "<pk> = ?" the two statements spelled inline before, so
    // single-PK SQL stays byte-identical; for a composite key it AND-joins every part in
    // declaration order — the same order pk_members binds in.
    //
    // Free function templates over the PK-member array rather than BaseStatement methods:
    // SchemaStatement sits at the cpp:S1448 35-method ceiling and BaseStatement is the
    // class both statements inherit, so anything added there is paid by every statement
    // type. Taking the array as a parameter also lets erase.cppm and update_grammar.cppm
    // call these without either one depending on the other.

    // Byte length of "<a> = ? AND <b> = ?" for the given PK members. Exact — both callers
    // size a ConstexprString with it, and ConstexprString truncates SILENTLY on overflow,
    // so an under-count is a wrong-SQL bug with no diagnostic. Column names go through
    // the canonical writer (#422) so an FK part is measured as "<name>_id".
    template <std::size_t N>
    consteval auto pk_where_clause_size(const std::array<std::meta::info, N>& pk_members) -> std::size_t {
        constexpr std::size_t EQUALS_PLACEHOLDER = 4; // " = ?"
        constexpr std::size_t AND_JOIN           = 5; // " AND "
        std::size_t           size               = 0;
        for (const std::meta::info member : pk_members) {
            size += storm::meta::column_name_size(member) + EQUALS_PLACEHOLDER;
        }
        return size + (AND_JOIN * (N - 1));
    }

    // Append "<a> = ? AND <b> = ?" for the given PK members, in declaration order.
    template <typename Buf, std::size_t N>
    consteval auto append_pk_where_clause(Buf& buf, const std::array<std::meta::info, N>& pk_members) -> void {
        bool first = true;
        for (const std::meta::info member : pk_members) {
            if (!first) {
                buf.append(" AND ");
            }
            storm::meta::append_column_name(buf, member); // #422 — FK parts emit "<name>_id"
            buf.append(" = ?");
            first = false;
        }
    }

    // Shared reflection utilities for all statement types
    template <typename T>
        requires storm::meta::Entity<T> && ModelWithPrimaryKey<T> && PrimaryKeyType<T> && ModelStorageAnnotated<T> &&
                 ModelFkPoliciesValid<T> && ModelAnnotationsValid<T> && ModelMaxLengthValid<T> &&
                 ModelPrimaryKeyValid<T>
    class BaseStatement {
      public:
        // Compile-time accessor for table name (used in SQL generation)
        static consteval auto get_table_name() -> std::string_view {
            return table_name_;
        }

      protected:
        // Helper to find primary key using storm::meta attributes
        static consteval auto find_primary_key_impl() -> std::meta::info {
            for (const std::meta::info member :
                 std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
                if (meta::is_primary_member(member)) {
                    return member;
                }
            }
            std::unreachable(); // never reached: ModelWithPrimaryKey<T> guarantees a primary key exists
        }

        // True when `member` is a primary-key COLUMN — a PK annotation on a member that is
        // actually persisted. m2m / reverse_fk containers are relation members, never
        // columns, so a PK annotation on one must not enter the key: it would put a name
        // in PRIMARY KEY (...) that no column definition ever emits (the #485 class of
        // leak). ModelPrimaryKeyValid rejects that model outright; this keeps the helpers
        // below consistent with all_members_ regardless.
        static consteval auto is_primary_key_column(std::meta::info member) -> bool {
            return meta::is_primary_member(member) && !meta::is_relation_field(member);
        }

        // How many members make up the primary key (#500) — 1 for a single-PK model,
        // N >= 2 for a composite one. Sizes the primary_key_members_ array below, so it
        // must be a separate consteval function (an array bound needs the count before
        // the elements are computed).
        static consteval auto primary_key_count() -> std::size_t {
            std::size_t count = 0;
            for (const std::meta::info member :
                 std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
                if (is_primary_key_column(member)) {
                    ++count;
                }
            }
            return count;
        }

        // Every primary-key member, in DECLARATION order — the order the table-level
        // PRIMARY KEY (a, b) clause emits, since column order is semantically
        // significant for the index it creates. For a single-PK model this is a
        // 1-element array whose sole element is primary_key_, so the existing
        // primary_key_ / pk_name_ statics (read by ~13 files) keep working unchanged.
        static consteval auto find_primary_key_members_impl() -> std::array<std::meta::info, primary_key_count()> {
            std::array<std::meta::info, primary_key_count()> members{};
            std::size_t                                      index = 0;
            for (const std::meta::info member :
                 std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
                if (is_primary_key_column(member)) {
                    members[index++] = member;
                }
            }
            return members;
        }

        // FK field detection utilities — an fk<...> class-template annotation (#431).
        static consteval auto is_fk_field(std::meta::info member) -> bool {
            return meta::is_fk_field(member);
        }

      public:
        // True when `member` is ANY part of the primary key (#501) — the widened form of
        // the `member == primary_key_` test that the UPDATE and upsert SET-target gates
        // used. On a composite model that old test excluded only the FIRST part, leaving
        // every other part writable: `SET product_id=? WHERE order_id=? AND product_id=?`
        // would rewrite part of the very key it matches on. Identical to the old test on a
        // single-PK model (the array has one element). Public because UpdateGrammar and
        // UpsertGrammar are separate types, not subclasses.
        static consteval auto is_pk_member(std::meta::info member) -> bool {
            // A CALLED consteval any_of lambda reports as uncovered against the 100%
            // line-coverage gate; the plain loop does not.
            for (const std::meta::info pk : primary_key_members_) { // NOLINT(readability-use-anyofallof)
                if (pk == member) {
                    return true;
                }
            }
            return false;
        }

      protected:
        // Per-attribute predicates forward to the storm_orm_field_attr leaf (#421),
        // the single source of truth for each flag-annotation test.
        static consteval auto is_unique_field(std::meta::info member) -> bool {
            return meta::is_unique(member);
        }

        static consteval auto is_indexed_field(std::meta::info member) -> bool {
            return meta::is_indexed(member);
        }

        static consteval auto is_auto_create_field(std::meta::info member) -> bool {
            return meta::is_auto_create(member);
        }

        static consteval auto is_auto_update_field(std::meta::info member) -> bool {
            return meta::is_auto_update(member);
        }

        // True when `member` should be stamped with now() on this operation: auto_update
        // always; auto_create on INSERT only (IsUpdate=false). auto_create on UPDATE is
        // false here so it binds the object's stored value (preserving created_at).
        static consteval auto stamps_now(std::meta::info member, bool is_update) -> bool {
            return is_auto_update_field(member) || (is_auto_create_field(member) && !is_update);
        }

        // Check if a field needs an index (indexed, unique, or fk — but not primary key).
        // FK is now an fk<...> class-template annotation (#431), checked separately.
        // Every PK member is excluded, not just primary_key_: the table-level
        // PRIMARY KEY (a, b) of a composite key (#500) already indexes each part, so a
        // separate single-column CREATE INDEX on a part would be redundant.
        static consteval auto needs_index(std::meta::info member) -> bool {
            if (meta::is_primary_member(member)) {
                return false;
            }
            if (meta::is_fk_field(member)) {
                return true;
            }
            return meta::is_indexed(member) || meta::is_unique(member);
        }

        // Get database column name for FK field: User sender → "sender_id".
        // Routes through the canonical column-name writer (#422) so the "_id" suffix
        // stays single-sourced (consteval — std::format is not constant-evaluated).
        static consteval auto get_fk_column_name(std::meta::info member) -> std::string {
            std::string name;
            meta::append_column_name(name, member);
            return name;
        }

      public:
        // Find primary key of a FK type (unwraps std::optional<T> → T first). Public so
        // the free two-query join helpers (join.cppm, #398) can extract FK columns.
        template <typename FKType>
            requires ValidForeignKey<FKType>
        static consteval auto find_fk_primary_key() -> std::meta::info {
            using InnerType = utilities::optional_inner_type_t<FKType>;
            for (const std::meta::info member :
                 std::meta::nonstatic_data_members_of(^^InnerType, std::meta::access_context::unchecked())) {
                if (meta::is_primary_member(member)) {
                    return member;
                }
            }
            std::unreachable(); // never reached: requires ModelWithPrimaryKey<...> guarantees a primary key exists
        }

      protected:
        // Number of PERSISTED fields. Relation container members (many-to-many #203,
        // reverse_fk #398) map to a separate query, not to a column, so they are
        // invisible to INSERT/SELECT/UPDATE/SCHEMA.
        static consteval auto get_field_count() -> std::size_t {
            std::size_t count = 0;
            for (const auto member :
                 std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
                if (!meta::is_relation_field(member)) {
                    ++count;
                }
            }
            return count;
        }

        // Pre-compute all persisted field members at compile-time (relation members
        // filtered — m2m #203, reverse_fk #398).
        template <std::size_t N> static consteval auto get_all_field_members() -> std::array<std::meta::info, N> {
            std::array<std::meta::info, N> result{};
            std::size_t                    idx = 0;
            for (const auto member :
                 std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
                if (!meta::is_relation_field(member) && idx < N) {
                    result[idx++] = member;
                }
            }
            return result;
        }

        // Field-name SQL grammar (#434): for_each_field_name + the size-calculators and
        // list-builders that produce "col1, col2, fk_id, ..." now live in
        // FieldNameGrammar<BaseStatement>. INSERT/SELECT call it directly; this class uses
        // it only to seed field_names_array_ below.
        using FieldNames = FieldNameGrammar<BaseStatement>;

      public:
        // Pre-computed field information - made public for QuerySet and JOIN optimization
        static constexpr auto field_count_ = get_field_count();
        // Makes the ModelWithPrimaryKey invariant explicit: a model with a primary-key
        // member always has >= 1 field, so the INSERT divides MAX_DB_VARIABLES / field_count_
        // can never divide by zero (issue #362, item A). Fires here if the concept is ever
        // loosened, instead of producing UB at the divide.
        static_assert(field_count_ >= 1, "A model must have at least one field (its primary key)");
        static constexpr auto           all_members_       = get_all_field_members<field_count_>();
        static constexpr auto           field_names_array_ = FieldNames::build_all_field_names_list();
        static inline const std::string field_names_       = std::string(field_names_array_);

        // True if T has any auto_create/auto_update field (#209). Gates the per-operation
        // system_clock::now() read so models with no timestamp fields pay zero overhead.
        static constexpr bool has_auto_timestamp_field_ = []() consteval {
            for (const auto m : all_members_) {
                if (is_auto_create_field(m) || is_auto_update_field(m)) {
                    return true;
                }
            }
            return false;
        }();

        // True if T has any many-to-many container field (#203/#391). Gates the
        // two-query eager-load path so models WITHOUT an m2m field never instantiate
        // it (scans raw members — m2m fields are filtered out of all_members_).
        static constexpr bool has_m2m_field_ = []() consteval {
            return std::ranges::any_of(
                    std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()),
                    [](std::meta::info m) { return meta::is_m2m_field(m); }
            );
        }();

        // True if T has any reverse_fk container field (#398). Gates the two-query
        // eager-load path the same way has_m2m_field_ does — a model without one
        // never instantiates the reverse-FK select machinery.
        static constexpr bool has_reverse_fk_field_ = []() consteval {
            return std::ranges::any_of(
                    std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()),
                    [](std::meta::info m) { return meta::is_reverse_fk_field(m); }
            );
        }();

        // One clock read per operation, but only for models that actually have a timestamp
        // field — otherwise the call compiles away entirely (no regression on plain models).
        [[nodiscard]] __attribute__((always_inline)) static auto batch_now() noexcept
                -> std::chrono::system_clock::time_point {
            if constexpr (has_auto_timestamp_field_) {
                return std::chrono::system_clock::now();
            } else {
                return {};
            }
        }

        // Reflection data - made public for JOIN statement access
        static constexpr auto primary_key_ = find_primary_key_impl();
        static constexpr auto pk_name_     = std::meta::identifier_of(primary_key_);
        static constexpr auto table_name_  = std::meta::identifier_of(^^T);

        // Composite primary key (#500). primary_key_members_ is the full PK list in
        // declaration order; primary_key_ / pk_name_ above stay the FIRST element, so
        // every single-PK caller is untouched. has_composite_pk_ branches the schema
        // generator (table-level PRIMARY KEY (a, b), #500), the by-key WHERE clause
        // (#501), and the INSERT column/bind policy (#502). JOIN on a composite
        // model is still out of scope (#504).
        static constexpr auto primary_key_members_ = find_primary_key_members_impl();
        static constexpr bool has_composite_pk_    = primary_key_members_.size() > 1;

        // UUID primary key support (#507): true iff the single PK member has UUID type
        static consteval auto has_uuid_pk_() -> bool {
            if constexpr (has_composite_pk_ || primary_key_count() == 0) {
                return false; // composite or no PK
            } else {
                constexpr auto pk_type = std::meta::type_of(primary_key_);
                return std::meta::dealias(pk_type) == std::meta::dealias(^^storm::orm::utilities::UUID);
            }
        }

        // How many columns the key spans, as a plain std::size_t (#501). Needed because
        // primary_key_members_ is an array of std::meta::info — a consteval-only type
        // that cannot be named at all in a runtime context, not even via .size(). The
        // bind path advances param_index by this at runtime.
        static constexpr std::size_t primary_key_column_count_ = primary_key_count();

      protected:
        // Index sequence utilities for compile-time field binding
        using field_indices_t = std::make_index_sequence<field_count_>;

        // Helper template for compile-time field binding with index sequence
        template <typename ConnType, typename Statement, std::size_t... Is>
        [[nodiscard]] static auto
        bind_all_fields_impl(Statement& stmt, const T& obj, std::index_sequence<Is...> /*unused*/) noexcept
                -> std::expected<void, typename ConnType::Error> {
            int                                           param_index = 1;
            std::expected<void, typename ConnType::Error> result{};
            const auto now = batch_now(); // shared by all fields of this object (compiles away if none)
            ((result = bind_field_at_index<ConnType, Is>(&stmt, obj, param_index, now), result.has_value()) && ...);
            return result;
        }

        // Helper template for INSERT binding (skips primary key for auto-increment)
        template <typename ConnType, typename Statement, std::size_t... Is>
        [[nodiscard]] static auto
        bind_non_pk_fields_impl(Statement& stmt, const T& obj, std::index_sequence<Is...> /*unused*/) noexcept
                -> std::expected<void, typename ConnType::Error> {
            int                                           param_index = 1;
            std::expected<void, typename ConnType::Error> result{};
            const auto now = batch_now(); // shared by all fields of this object (compiles away if none)
            ((result = bind_field_at_index<ConnType, Is, true>(&stmt, obj, param_index, now), result.has_value()) &&
             ...);
            return result;
        }

        // Does the caller's PK-skip policy exclude `member` from the bind order?
        // SkipAllPK covers every part of a composite key (#501) — the UPDATE policy,
        // whose SET clause omits the whole key. Plain SkipPK is the INSERT policy: it
        // skips the DB-generated key, which only a single-column PK can be — a
        // composite key has no auto-generation mechanism, so every part is caller
        // data and nothing is skipped (#502). The two policies coincide on a
        // single-PK model.
        template <bool SkipPK, bool SkipAllPK> static consteval auto skips_pk_column(std::meta::info member) -> bool {
            if (!SkipPK) {
                return false;
            }
            if (SkipAllPK) {
                return is_pk_member(member);
            }
            return !has_composite_pk_ && member == primary_key_;
        }

        // Unified field binder: binds a single field at compile-time index.
        // SkipPK=true skips primary key fields (for INSERT/UPDATE non-PK binding).
        // IsUpdate=true marks the UPDATE path so auto_create fields bind the object's
        // stored value instead of now() (#209). `now` is read once per operation by the
        // caller and threaded in so every row in a batch shares the same timestamp.
        // Auto-increments param_index on successful bind.
        //
        // SkipAllPK widens the skip from primary_key_ to EVERY primary-key member (#501),
        // which is what UPDATE needs: its SET clause omits the whole composite key, so the
        // bind order must too. INSERT keeps plain SkipPK, which since #502 skips NOTHING
        // on a composite model (see skips_pk_column: every key part is caller data).
        // No effect on single-PK models, where the two flags coincide.
        template <
                typename ConnType,
                std::size_t Index,
                bool        SkipPK    = false,
                bool        IsUpdate  = false,
                bool        SkipAllPK = false>
        [[nodiscard]] __attribute__((always_inline)) static constexpr auto bind_field_at_index(
                typename ConnType::Statement*         stmt,
                const T&                              obj,
                int&                                  param_index,
                std::chrono::system_clock::time_point now = {}
        ) noexcept -> std::expected<void, typename ConnType::Error> {
            constexpr auto member = all_members_[Index];

            // Auto-timestamp fields (#209) must be system_clock::time_point — binding now()
            // to any other type is a model error. Fires at the call site with a clear message.
            if constexpr (is_auto_create_field(member) || is_auto_update_field(member)) {
                static_assert(
                        ValidTimestampField<member>,
                        "auto_create / auto_update fields must be std::chrono::system_clock::time_point"
                );
            }

            // Compile-time PK skip for INSERT/UPDATE non-PK paths.
            if constexpr (skips_pk_column<SkipPK, SkipAllPK>(member)) {
                return {};
            }
            // Auto-timestamp (#209): stamp now() for auto_update (always) and auto_create
            // (INSERT only). auto_create on UPDATE is not stamped here and falls through to
            // the normal bind below, preserving the object's stored created_at.
            else if constexpr (stamps_now(member, IsUpdate)) {
                return bind_one<ConnType>(stmt, param_index, now);
            }
            // FK field - extract and bind the PK value from the foreign object
            else if constexpr (is_fk_field(member)) {
                return bind_fk_field_at_index<ConnType, Index>(stmt, obj, param_index);
            }
            // full_unsigned field (#436): order-preserving storage — bind a 20-char
            // zero-padded decimal string instead of the signed int64 hot path. uint64_max
            // (18446744073709551615) is 20 digits, so every value pads to the same width
            // and lexicographic order == numeric order (SQLite TEXT); PG NUMERIC(20,0)
            // parses the same string, leading zeros ignored. Optional binds NULL via the
            // optional<string> dispatch in bind_parameter_value. Slow path (format +
            // bind_text), paid only by full_unsigned fields.
            else if constexpr (storm::meta::has_full_unsigned_attr(member)) {
                using FieldType = std::remove_cvref_t<decltype(obj.[:member:])>;
                if constexpr (utilities::is_optional_v<FieldType>) {
                    return bind_one<ConnType>(
                            stmt, param_index, obj.[:member:]
                                    .has_value()
                                            ? std::optional<std::string>(std::format("{:020}", obj.[:member:].value()))
                                            : std::nullopt
                    );
                } else {
                    return bind_one<ConnType>(stmt, param_index, std::format("{:020}", obj.[:member:]));
                }
            } else {
                return bind_optional_or_uuid_pk_field<ConnType, Index>(stmt, obj, param_index);
            }
        }

        // Bind optional or UUID PK fields. Gates UUID PKs to reject empty.
        template <typename ConnType, std::size_t Index>
        [[nodiscard]] __attribute__((always_inline)) static constexpr auto
        bind_optional_or_uuid_pk_field(typename ConnType::Statement* stmt, const T& obj, int& param_index) noexcept
                -> std::expected<void, typename ConnType::Error> {
            constexpr auto member = all_members_[Index];
            if constexpr (is_pk_member(member) &&
                          std::meta::dealias(std::meta::type_of(member)) == ^^storm::orm::utilities::UUID) {
                // UUID PK: reject empty, no auto-generation. Non-PK UUID columns auto-generate via bind_uuid.
                return utilities::bind_uuid_pk<typename ConnType::Statement, typename ConnType::Error>(
                        stmt, param_index, obj.[:member:]
                );
            } else {
                return bind_one<ConnType>(stmt, param_index, obj.[:member:]);
            }
        }

        // Bind one value at param_index and advance it on success. Shared tail used by the
        // plain-field and auto-timestamp branches of bind_field_at_index.
        template <typename ConnType>
        [[nodiscard]] __attribute__((always_inline)) static constexpr auto
        bind_one(typename ConnType::Statement* stmt, int& param_index, const auto& value) noexcept
                -> std::expected<void, typename ConnType::Error> {
            auto result = bind_value_by_type<ConnType>(*stmt, param_index, value);
            if (!result) {
                return std::unexpected(result.error());
            }
            ++param_index;
            return {};
        }

        // Extract and bind the foreign object's PK value (NULL for an empty optional FK).
        template <typename ConnType, std::size_t Index>
        [[nodiscard]] __attribute__((always_inline)) static constexpr auto
        bind_fk_field_at_index(typename ConnType::Statement* stmt, const T& obj, int& param_index) noexcept
                -> std::expected<void, typename ConnType::Error> {
            constexpr auto member = all_members_[Index];
            using FKType          = std::remove_cvref_t<decltype(obj.[:member:])>;
            if constexpr (utilities::is_optional_v<FKType>) {
                // Optional FK: bind NULL when empty, otherwise bind the inner PK value
                if (!obj.[:member:].has_value()) {
                    auto null_result = stmt->bind_null(param_index);
                    if (!null_result) {
                        return std::unexpected(null_result.error());
                    }
                    ++param_index;
                    return {};
                }
                constexpr auto fk_pk_member = find_fk_primary_key<FKType>();
                return bind_one<ConnType>(stmt, param_index, obj.[:member:].value().[:fk_pk_member:]);
            } else {
                constexpr auto fk_pk_member = find_fk_primary_key<FKType>();
                return bind_one<ConnType>(stmt, param_index, obj.[:member:].[:fk_pk_member:]);
            }
        }

        // Bulk binding scaffolding: iterate objects, fold the per-field bind over the
        // index sequence, return on first error. Bound to bind_field_at_index<...>
        // through the SkipPrimaryKey template parameter so a single body serves
        // both the all-fields and non-PK variants.
        template <bool SkipPrimaryKey, typename ConnType, typename Statement, typename ContainerType, std::size_t... Is>
        [[nodiscard]] static auto bind_bulk_objects_impl(
                Statement& stmt, const ContainerType& objects, std::index_sequence<Is...> /*unused*/
        ) noexcept -> std::expected<void, typename ConnType::Error> {
            int param_index = 1;
            // One now() per batch, reused for every row (#209): all rows share the same
            // created_at/updated_at, matching the issue's batch-timestamp intent.
            const auto now = batch_now();

            for (const auto& obj : objects) {
                std::expected<void, typename ConnType::Error> result{};
                ((result = bind_field_at_index<ConnType, Is, SkipPrimaryKey>(&stmt, obj, param_index, now),
                  result.has_value()) &&
                 ...);
                if (!result) {
                    return result;
                }
            }
            return {};
        }

        // Helper for bulk binding multiple objects with index sequence
        template <typename ConnType, typename Statement, typename ContainerType, std::size_t... Is>
        [[nodiscard]] static auto bind_all_objects_bulk_impl(
                Statement& stmt, const ContainerType& objects, std::index_sequence<Is...> seq
        ) noexcept -> std::expected<void, typename ConnType::Error> {
            return bind_bulk_objects_impl<false, ConnType, Statement, ContainerType, Is...>(stmt, objects, seq);
        }

        // Helper for bulk INSERT binding (skips PK for auto-increment)
        template <typename ConnType, typename Statement, typename ContainerType, std::size_t... Is>
        [[nodiscard]] static auto bind_non_pk_objects_bulk_impl(
                Statement& stmt, const ContainerType& objects, std::index_sequence<Is...> seq
        ) noexcept -> std::expected<void, typename ConnType::Error> {
            return bind_bulk_objects_impl<true, ConnType, Statement, ContainerType, Is...>(stmt, objects, seq);
        }

      public:
        // Common batch operation thresholds. Public since #501: EraseGrammar derives the
        // bulk-DELETE chunk size from it and is a separate type, not a subclass. It is a
        // backend constant rather than model state, so exposing it leaks nothing.
        static constexpr std::size_t MAX_DB_VARIABLES = 999;

      protected:
        // Adaptive threshold calculation based on batch size and field count
        // Returns the optimal threshold for deciding between bulk SQL and individual inserts
        static constexpr auto calculate_adaptive_threshold(std::size_t batch_size, std::size_t max_bulk_size)
                -> std::size_t {
            using utilities::batch::FALLBACK_BATCH_SIZE;
            using utilities::batch::SMALL_THRESHOLD;

            // For very small batches, always use bulk SQL up to the SQLite limit
            if (batch_size <= SMALL_THRESHOLD) {
                return max_bulk_size;
            }

            // Calculate safe thresholds based on max_bulk_size (which already accounts for field count)
            // max_bulk_size = 999 / field_count, so we scale our thresholds accordingly

            // For small-medium batches, use bulk SQL if safe
            // Use 50% of max_bulk_size as the sweet spot for bulk operations
            const std::size_t bulk_sweet_spot = std::max(FALLBACK_BATCH_SIZE, max_bulk_size / 2);

            if (batch_size <= bulk_sweet_spot) {
                return bulk_sweet_spot; // Use bulk SQL - most efficient
            }

            // For medium batches, try to use bulk if within 80% of SQLite limit
            const std::size_t bulk_max_safe = (max_bulk_size * 4) / 5; // 80% of max

            if (batch_size <= bulk_max_safe) {
                return bulk_max_safe; // Push bulk SQL to near SQLite limit
            }

            // For large batches (>80% of SQLite limit), use individual inserts with transaction
            // Avoids hitting SQLite variable limits and better memory usage
            return FALLBACK_BATCH_SIZE; // Force individual insert path - safe for any field count
        }

        // Common binding utilities for different types
        // Delegates to unified bind_parameter_value in utilities
        template <typename ConnType>
        [[nodiscard]] static auto
        bind_value_by_type(typename ConnType::Statement& stmt, int param_index, const auto& value) noexcept
                -> std::expected<void, typename ConnType::Error> {
            return utilities::bind_parameter_value<typename ConnType::Statement, typename ConnType::Error>(
                    stmt, param_index, value
            );
        }

        // Bind every primary-key value of `obj`, in declaration order, starting at
        // `param_index` and advancing it past the last part (#501). The bind order matches
        // the column order append_pk_where_clause emits, which is what keeps the
        // placeholders and the values aligned.
        //
        // The index sequence is over primary_key_members_ rather than a runtime loop
        // because each part may be a DIFFERENT type (int + std::string is the canonical
        // composite key), so the bind has to be dispatched per part at compile time. An FK
        // part binds the referenced object's own PK value, matching how the "<name>_id"
        // column is written.
        // Each part's placeholder is `param_index + Is` — a compile-time offset from the
        // start of the key, so no running counter is threaded through the fold. On a
        // single-PK model this collapses to exactly one `bind(stmt, param_index, value)`
        // call, identical to the hand-written bind it replaced.
        template <typename ConnType, std::size_t... Is>
        [[nodiscard]] __attribute__((always_inline)) static auto bind_pk_values_impl(
                typename ConnType::Statement& stmt, const T& obj, int param_index, std::index_sequence<Is...> /*unused*/
        ) noexcept -> std::expected<void, typename ConnType::Error> {
            std::expected<void, typename ConnType::Error> result{};
            ((result = bind_one_pk_part<ConnType, primary_key_members_[Is]>(
                      stmt, obj, param_index + static_cast<int>(Is)
              ),
              result.has_value()) &&
             ...);
            return result;
        }

        // Bind one primary-key part at `index`. An FK part (the canonical
        // association-table shape) stores the referenced row's key, so bind THAT, not the
        // whole object — the same value the "<name>_id" column holds.
        //
        // Takes `index` BY VALUE and does not advance it: the caller computes each part's
        // offset as a compile-time constant (see bind_pk_values_impl). Flat code, no
        // lambda and no dead increment on the last part — this sits directly in the
        // single-row UPDATE/DELETE hot path, where nested lambdas have cost ~3-4% before.
        template <typename ConnType, std::meta::info Member>
        [[nodiscard]] __attribute__((always_inline)) static auto
        bind_one_pk_part(typename ConnType::Statement& stmt, const T& obj, int index) noexcept
                -> std::expected<void, typename ConnType::Error> {
            if constexpr (is_fk_field(Member)) {
                using FKType = std::remove_cvref_t<decltype(obj.[:Member:])>;
                return bind_value_by_type<ConnType>(stmt, index, obj.[:Member:].[:find_fk_primary_key<FKType>():]);
            } else {
                return bind_value_by_type<ConnType>(stmt, index, obj.[:Member:]);
            }
        }

        // Bind the whole primary key of `obj` starting at `param_index`, advancing it past
        // the key. The entry point erase.cppm and update.cppm call; single-PK models bind
        // exactly one value, as before.
        //
        // The advance is a single add of a compile-time constant, applied once here rather
        // than per-part inside the fold — on the single-PK path the caller never reads it
        // back, so it folds away entirely.
        template <typename ConnType>
        [[nodiscard]] __attribute__((always_inline)) static auto
        bind_pk_values(typename ConnType::Statement& stmt, const T& obj, int& param_index) noexcept
                -> std::expected<void, typename ConnType::Error> {
            auto result = bind_pk_values_impl<ConnType>(
                    stmt, obj, param_index, std::make_index_sequence<primary_key_column_count_>{}
            );
            if (result.has_value()) {
                param_index += static_cast<int>(primary_key_column_count_);
            }
            return result;
        }

        // =====================================================================
        // COLUMN EXTRACTION HELPERS - Moved here from SelectStatement so that
        // constexpr access to all_members_[Index] happens in base.cppm context
        // (avoids P2996 experimental compiler limitation in select.cppm module)
        // =====================================================================

        // Extract optional FK column: set nullopt when NULL, otherwise extract inner PK
        template <std::size_t Index, typename Statement, typename FieldType>
        __attribute__((always_inline)) static void extract_optional_fk_column(Statement* stmt, T& obj) noexcept {
            constexpr auto member       = all_members_[Index];
            using InnerFKType           = utilities::optional_inner_type_t<FieldType>;
            constexpr auto fk_pk_member = find_fk_primary_key<FieldType>();
            using PKType                = std::remove_cvref_t<decltype(std::declval<InnerFKType>().[:fk_pk_member:])>;
            if (stmt->is_null(Index)) {
                obj.[:member:] = std::nullopt;
            } else {
                InnerFKType fk_inner{};
                fk_inner.[:fk_pk_member:] = ColumnExtractor::extract_column_value<PKType>(stmt, Index);
                obj.[:member:]            = std::move(fk_inner);
            }
        }

        // Extract single column into obj at compile-time index
        // Statement is deduced from stmt pointer; all_members_[Index] is valid here
        template <std::size_t Index, typename Statement>
        __attribute__((always_inline)) static void extract_column_fast(Statement* stmt, T& obj) noexcept {
            if constexpr (Index < field_count_) {
                constexpr auto member = all_members_[Index];
                using FieldType       = std::remove_cvref_t<decltype(obj.[:member:])>;
                if constexpr (is_fk_field(member)) {
                    if constexpr (utilities::is_optional_v<FieldType>) {
                        extract_optional_fk_column<Index, Statement, FieldType>(stmt, obj);
                    } else {
                        obj.[:member:]              = FieldType{};
                        constexpr auto fk_pk_member = find_fk_primary_key<FieldType>();
                        using PKType                = std::remove_cvref_t<decltype(obj.[:member:].[:fk_pk_member:])>;
                        obj.[:member:].[:fk_pk_member:] = ColumnExtractor::extract_column_value<PKType>(stmt, Index);
                    }
                } else if constexpr (storm::meta::has_full_unsigned_attr(member)) {
                    extract_full_unsigned_into<member, FieldType>(stmt, obj, Index);
                } else {
                    obj.[:member:] = ColumnExtractor::extract_column_value<FieldType>(stmt, Index);
                }
            }
        }

        // Expand index sequence and extract each column
        template <typename Statement, std::size_t... Is>
        __attribute__((always_inline)) static void
        extract_all_columns_impl(Statement* stmt, T& obj, std::index_sequence<Is...> /*unused*/) noexcept {
            ((extract_column_fast<Is>(stmt, obj)), ...);
        }

        // Entry point: extract all columns into obj using field_indices_t
        template <typename Statement>
        __attribute__((always_inline)) static void extract_all_columns(Statement* stmt, T& obj) noexcept {
            extract_all_columns_impl(stmt, obj, field_indices_t{});
        }

        // =====================================================================
        // SQL CLAUSE HELPERS - Shared across SELECT, DISTINCT, AGGREGATE
        // =====================================================================

        // LCOV_EXCL_START — PostgreSQL-only; covered by CI PG tests, not local SQLite mock
        // Helper: Adapt ORDER BY SQL for PostgreSQL NULL ordering semantics
        // Adds NULLS FIRST after ASC and NULLS LAST after DESC to match SQLite behavior
        static void adapt_order_by_for_pg(std::string& adapted) {
            using namespace std::string_view_literals;
            // Token lengths derived from the literals so the offsets can never drift (ES.45).
            static constexpr auto kAsc        = " ASC"sv;
            static constexpr auto kDesc       = " DESC"sv;
            static constexpr auto kNulls      = " NULLS"sv; // already-adapted guard prefix
            static constexpr auto kNullsFirst = " NULLS FIRST"sv;
            static constexpr auto kNullsLast  = " NULLS LAST"sv;

            std::size_t pos = 0;
            while ((pos = adapted.find(kAsc, pos)) != std::string::npos) {
                std::size_t const after = pos + kAsc.size();
                if (adapted.substr(after, kNulls.size()) != kNulls) {
                    adapted.insert(after, kNullsFirst);
                }
                pos = after + kNullsFirst.size();
            }
            pos = 0;
            while ((pos = adapted.find(kDesc, pos)) != std::string::npos) {
                std::size_t const after = pos + kDesc.size();
                if (adapted.substr(after, kNulls.size()) != kNulls) {
                    adapted.insert(after, kNullsLast);
                }
                pos = after + kNullsLast.size();
            }
        }
        // LCOV_EXCL_STOP

      public:
        // ORDER BY / LIMIT / OFFSET appenders are public so the free two-query join
        // helpers (join.cppm, #398) can assemble Q1/Q2 base clauses for any model.

        // Helper: Append ORDER BY clause to SQL from wrapper
        // NOTE: ORDER BY must come before LIMIT/OFFSET in SQLite
        // For PostgreSQL, adds NULLS FIRST/LAST to match SQLite semantics
        template <typename ConnTypeForDialect = void>
        __attribute__((always_inline)) static void
        append_order_by(std::string& sql, const std::optional<OrderByWrapper>& order_by_wrapper) {
            if (order_by_wrapper.has_value() && !order_by_wrapper->empty()) {
                const auto& order_sql = order_by_wrapper->get_order_by_sql();
                if constexpr (db::SupportsPgDialect<ConnTypeForDialect>) {
                    std::string adapted = order_sql; // LCOV_EXCL_LINE — PG-only
                    adapt_order_by_for_pg(adapted);  // LCOV_EXCL_LINE — PG-only
                    sql += adapted;                  // LCOV_EXCL_LINE — PG-only
                } else {
                    sql += order_sql;
                }
            }
        }

        // Helper: Append LIMIT/OFFSET clauses to SQL
        // NOTE: SQLite requires LIMIT when using OFFSET, so we use LIMIT -1 (meaning unlimited)
        // PostgreSQL uses LIMIT ALL for unlimited rows
        template <typename ConnTypeForDialect = void>
        __attribute__((always_inline)) static void
        append_limit_offset(std::string& sql, const std::optional<int>& limit, const std::optional<int>& offset) {
            if (limit.has_value()) {
                sql += " LIMIT ";
                sql += std::to_string(limit.value());
            } else if (offset.has_value()) {
                // Need LIMIT when using OFFSET
                if constexpr (db::SupportsLimitAll<ConnTypeForDialect>) {
                    if constexpr (ConnTypeForDialect::supports_limit_all) {
                        sql += " LIMIT ALL";
                    } else {
                        sql += " LIMIT -1";
                    }
                } else {
                    sql += " LIMIT -1"; // Default: SQLite-compatible
                }
            }

            if (offset.has_value()) {
                sql += " OFFSET ";
                sql += std::to_string(offset.value());
            }
        }

      protected:
        // Helper: Bind WHERE expression parameters to statement
        // Returns std::expected<void, Error> - resets statement on failure
        // Helper: bind a WHERE-style expression and convert the result. On
        // failure, reset the statement and propagate as std::unexpected.
        // Used by both bind_where_params (starts at param_index = 1) and
        // bind_having_params (continues from WHERE's last index).
        template <typename Statement, typename Error>
        [[nodiscard]] __attribute__((always_inline)) static auto
        bind_expr_or_reset(Statement* stmt_ptr, const orm::where::ExpressionVariantPtr& expr, int& param_index)
                -> std::expected<void, Error> {
            auto bind_result = orm::where::bind_params_direct<Statement, Error>(*expr, stmt_ptr, param_index);
            if (!bind_result) [[unlikely]] {
                stmt_ptr->reset();
                return std::unexpected(bind_result.error());
            }
            return {};
        }

        template <typename Statement, typename Error>
        [[nodiscard]] __attribute__((always_inline)) static auto
        bind_where_params(Statement* stmt_ptr, const orm::where::ExpressionVariantPtr& where_expr)
                -> std::expected<void, Error> {
            int param_index = 1;
            return bind_expr_or_reset<Statement, Error>(stmt_ptr, where_expr, param_index);
        }

        // Helper: Bind HAVING expression parameters to statement
        // param_index continues from WHERE's last index (or starts at 1 if no WHERE)
        template <typename Statement, typename Error>
        [[nodiscard]] __attribute__((always_inline)) static auto
        bind_having_params(Statement* stmt_ptr, const orm::where::ExpressionVariantPtr& having_expr, int& param_index)
                -> std::expected<void, Error> {
            return bind_expr_or_reset<Statement, Error>(stmt_ptr, having_expr, param_index);
        }
    };

} // namespace storm::orm::statements
