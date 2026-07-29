module;

#include <meta>

export module storm_orm_field_attr;

import std;

// Dependency-free leaf module (#387): single source of truth for the field
// annotation objects, shared by statement modules and the public storm module.
// Same pattern as storm_db_concept — no Storm imports, so any module may use it
// without creating a cycle.
export namespace storm::meta {

    // Field annotation flags (#492) are free-standing class-type annotation objects,
    // mirroring how the FK annotation fk<> already works — each flag is an empty tag
    // struct plus an `inline constexpr` object, read via C++26 reflection, e.g.
    //   [[= storm::primary]] int id;
    //   [[= storm::unique]]  std::string email;
    // They replace the former `enum class FieldAttr` (breaking, no dual-spelling): the
    // free-standing spelling now reads uniformly next to fk<>/many_to_many<>. The
    // per-member exclusivity the enum gave for free (a field could carry at most one
    // enumerator) is recovered by the ModelAnnotationsValid<T> concept in base.cppm,
    // which rejects conflicting combinations at compile time.
    //
    // NOTE: foreign keys use the templated annotation storm::meta::fk<RefAction> (below),
    // because the FK carries an optional ON DELETE policy (#431).
    struct Primary {};              // plain INTEGER PRIMARY KEY
    struct PrimaryAutoincrement {}; // SQLite never-reuse rowid opt-in (#379)
    // One column of a composite (multi-column) primary key (#500). Deliberately a
    // SEPARATE tag from Primary rather than "two `primary` members means composite":
    // reusing Primary would silently turn a double-`primary` typo into a legal
    // composite key, whereas ModelAnnotationsValid keeps that an error. Two or more
    // primary_part members produce a table-level PRIMARY KEY (a, b); exactly one is
    // rejected (that is a plain `primary`), as is any mix with primary /
    // primary_autoincrement — see ModelPrimaryKeyValid in base.cppm.
    struct PrimaryPart {};
    struct Indexed {};    // CREATE INDEX on the column
    struct Unique {};     // UNIQUE constraint on the column
    struct AutoCreate {}; // stamp now() on INSERT only (#209)
    struct AutoUpdate {}; // stamp now() on INSERT and UPDATE (#209)
    // 64-bit unsigned storage opt-ins (#436). A bare unsigned-64 field is a
    // compile-time error; it must carry exactly one of these:
    //   signed_storage — keep today's signed BIGINT/INTEGER (byte-identical, fast)
    //                    for values always <= INT64_MAX.
    //   full_unsigned  — order-preserving full-range storage: PG NUMERIC(20,0),
    //                    SQLite zero-padded 20-char TEXT (lexicographic == numeric).
    struct SignedStorage {};
    struct FullUnsigned {};

    // NOLINTBEGIN(readability-identifier-length) — short names are the public annotation spellings
    inline constexpr Primary              primary{};
    inline constexpr PrimaryAutoincrement primary_autoincrement{};
    inline constexpr PrimaryPart          primary_part{};
    inline constexpr Indexed              indexed{};
    inline constexpr Unique               unique{};
    inline constexpr AutoCreate           auto_create{};
    inline constexpr AutoUpdate           auto_update{};
    inline constexpr SignedStorage        signed_storage{};
    inline constexpr FullUnsigned         full_unsigned{};
    // NOLINTEND(readability-identifier-length)

    // True when `member` carries an annotation of the tag type `Tag` (#492). The single
    // primitive behind every flag predicate below — the same annotations_of scan
    // is_fk_field uses, generalised over the tag type. Matches on exact type identity,
    // so unrelated annotations (fk<>, many_to_many<>) never collide.
    // A plain loop (not std::ranges::any_of): the any_of lambda form leaves the
    // predicate body attributed as an uncovered consteval-only function under the
    // coverage gate, whereas the loop is fully covered. NOLINT silences the
    // readability-use-anyofallof suggestion, which would reintroduce that gap.
    template <typename Tag> consteval auto has_annotation_type(std::meta::info member) -> bool {
        for (const auto annotation : std::meta::annotations_of(member)) { // NOLINT(readability-use-anyofallof)
            if (std::meta::type_of(annotation) == ^^Tag) {
                return true;
            }
        }
        return false;
    }

    // Referential action for a foreign key's ON DELETE policy (#431). Carried as the
    // template argument of the FK annotation `fk<RefAction>` (and of `many_to_many<RefAction>`
    // for the auto-junction). `Restrict` is the SQL default and Storm's pre-#431 behaviour:
    // a bare `fk<>` emits no ON DELETE clause at all, so existing DDL is byte-identical.
    enum class RefAction : std::uint8_t {
        Cascade,  // delete the child rows when the parent row is deleted
        SetNull,  // set the child FK column to NULL (requires a nullable FK)
        Restrict, // block the parent delete while children exist (SQL default)
        NoAction, // defer the check; like Restrict for immediate constraints
    };

    // The SQL keyword phrase for a RefAction, e.g. RefAction::SetNull → "SET NULL".
    // Longest phrase ("NO ACTION") is 9 chars; " ON DELETE NO ACTION" is 20.
    consteval auto ref_action_sql(RefAction action) -> std::string_view {
        using enum RefAction;
        switch (action) {
        case Cascade:
            return "CASCADE";
        case SetNull:
            return "SET NULL";
        case Restrict:
            return "RESTRICT";
        case NoAction:
            return "NO ACTION";
        }
        return {};
    }

    // True when `member` carries storm::primary_part — one column of a composite
    // primary key (#500). Distinct from is_primary_member below: a plain `primary`
    // member is NOT a primary part, so the schema generator can tell the
    // column-level and table-level PRIMARY KEY paths apart.
    consteval auto is_primary_part_member(std::meta::info member) -> bool {
        return has_annotation_type<PrimaryPart>(member);
    }

    // A field is "a primary key" for any of the annotation variants: plain `primary`
    // (plain INTEGER PRIMARY KEY), `primary_autoincrement` (the SQLite never-reuse
    // opt-in, #379), or `primary_part` (one column of a composite key, #500). Every
    // PK-detection site routes through here so the variants can never drift apart.
    consteval auto is_primary_member(std::meta::info member) -> bool {
        return has_annotation_type<Primary>(member) || has_annotation_type<PrimaryAutoincrement>(member) ||
               is_primary_part_member(member);
    }

    // True when `member` carries storm::primary_autoincrement (the SQLite never-reuse
    // rowid opt-in, #379) — drives the AUTOINCREMENT keyword in schema.cppm.
    consteval auto is_primary_autoincrement(std::meta::info member) -> bool {
        return has_annotation_type<PrimaryAutoincrement>(member);
    }

    // Issue #478: ValidFieldInfo is the compile-time gate that a std::meta::info
    // NTTP names a real field — a non-static data member with an identifier — the
    // exact precondition the field selector f<> and every flag predicate below
    // assume of their `member` argument. It gives that precondition a name so a bad
    // NTTP (a static member, a member function, or a whole type reflection) fails at
    // the named constraint instead of deep inside identifier_of/type_of. Unlike the
    // Entity structural gate, the requirements here are load-bearing on this
    // clang-p2996 build: is_nonstatic_data_member(^^int) and
    // is_nonstatic_data_member(^^Type::static_member) are both false, so the concept
    // genuinely rejects. has_identifier mirrors the f<> precondition and guards the
    // identifier_of use in the field proxies against an unnamed member — a case not
    // reachable through a ^^ NTTP today, but named so the contract is explicit.
    template <std::meta::info MemberInfo>
    concept ValidFieldInfo = std::meta::is_nonstatic_data_member(MemberInfo) && std::meta::has_identifier(MemberInfo);

    // Per-attribute field predicates (#421): the single source of truth for the
    // per-flag annotation test, so the same check cannot drift between statement
    // modules. Each scans annotations_of for its tag type (#492). FK detection lives
    // in is_fk_field below (an fk<...> class-template annotation).
    consteval auto is_unique(std::meta::info member) -> bool {
        return has_annotation_type<Unique>(member);
    }

    consteval auto is_indexed(std::meta::info member) -> bool {
        return has_annotation_type<Indexed>(member);
    }

    // auto_create stamps now() on INSERT only; auto_update on both INSERT and UPDATE (#209).
    consteval auto is_auto_create(std::meta::info member) -> bool {
        return has_annotation_type<AutoCreate>(member);
    }

    consteval auto is_auto_update(std::meta::info member) -> bool {
        return has_annotation_type<AutoUpdate>(member);
    }

    // True when the reflected TYPE `type` is a std::optional<T> specialization (#509).
    // The detection core shared by every optional-related query in the tree.
    //
    // PRECONDITION: `type` is already dealiased — an alias to std::optional<T> does
    // NOT match (there is no internal dealias, so the normalization step stays owned
    // by one layer instead of being applied twice). Every caller honours it: the
    // sites that start from a member go through is_optional_member below, and the
    // two unwrapping predicates in this file plus unwrap_optional_type in
    // statements/base.cppm all dealias into their local `t` first.
    //
    // Reflection-level comparison rather than `if constexpr`, so it stays usable
    // inside a consteval loop where the loop variable is not a core constant
    // expression — the same discipline as is_unsigned64_member below.
    // has_template_arguments is the required guard: template_of is only valid on
    // a specialization, and most types are not one.
    consteval auto is_optional_type(std::meta::info type) -> bool {
        return std::meta::has_template_arguments(type) && std::meta::template_of(type) == ^^std::optional;
    }

    // True when the data member `member` has type std::optional<T> (#509).
    // The single source of truth for the structural nullability query, which was
    // previously hand-rolled at five sites here and in statements/base.cppm, each
    // with its own spelling of the same two reflection calls.
    //
    // The member-level spelling is what the sites starting from a data member need:
    // keeping type_of/dealias inside means neither step can be forgotten, and it is
    // what satisfies is_optional_type's already-dealiased precondition for them. The
    // sites that already hold a dealiased type (the two unwrapping predicates below,
    // and unwrap_optional_type in statements/base.cppm) call the core directly.
    consteval auto is_optional_member(std::meta::info member) -> bool {
        return is_optional_type(std::meta::dealias(std::meta::type_of(member)));
    }

    // A 64-bit unsigned source type — the set that needs an explicit storage
    // annotation (#436). Signed-64 and all smaller types are unaffected.
    template <typename T>
    concept Unsigned64 = std::is_same_v<T, std::uint64_t> || std::is_same_v<T, unsigned long> ||
                         std::is_same_v<T, unsigned long long>;

    // True when the data member `member` has a 64-bit unsigned type (#436), looking
    // through std::optional<>. Reflection-level type comparison (not `if constexpr`),
    // so it is usable inside a consteval loop where the loop variable is not a core
    // constant expression. std::uint64_t is a typedef for one of the two canonical
    // unsigned types below (unsigned long on LP64), so comparing the dealiased member
    // type against both covers it — and `^^` can only reflect real types, not the
    // std::uint64_t using-declarator.
    consteval auto is_unsigned64_member(std::meta::info member) -> bool {
        std::meta::info t = std::meta::dealias(std::meta::type_of(member));
        if (is_optional_type(t)) {
            t = std::meta::dealias(std::meta::template_arguments_of(t)[0]);
        }
        return t == ^^unsigned long || t == ^^unsigned long long;
    }

    // True when `member` carries storm::full_unsigned (order-preserving storage).
    consteval auto has_full_unsigned_attr(std::meta::info member) -> bool {
        return has_annotation_type<FullUnsigned>(member);
    }

    // True when `member` carries storm::signed_storage (legacy signed storage).
    consteval auto has_signed_storage_attr(std::meta::info member) -> bool {
        return has_annotation_type<SignedStorage>(member);
    }

    // Foreign-key annotation (#431). A class-template annotation carries the optional
    // ON DELETE policy (RefAction); the flag annotations cannot carry a parameter, and an FK
    // needs the extra parameter. It lives in this leaf module so every statement module
    // (schema, join, field_names, distinct, base) can detect FK fields without importing
    // each other.
    //   [[= storm::meta::fk<>]] Person sender;                       // bare FK (RESTRICT)
    //   [[= storm::meta::fk<RefAction::Cascade>]] Person owner;      // ON DELETE CASCADE
    //   [[= storm::meta::fk<RefAction::SetNull>]] std::optional<Person> owner;  // SET NULL
    // The default RefAction::Restrict matches the SQL default, so bare fk<> emits a plain
    // REFERENCES with no ON DELETE clause (byte-identical to pre-#431 DDL).
    template <RefAction Action = RefAction::Restrict> struct Fk {
        static constexpr RefAction on_delete = Action;
    };
    // NOLINTNEXTLINE(readability-identifier-length) — `fk` is the public annotation spelling
    template <RefAction Action = RefAction::Restrict> inline constexpr Fk<Action> fk{};

    // Reflection of the Fk<...> annotation TYPE carried by `member`, if any.
    consteval auto fk_annotation_type_of(std::meta::info member) -> std::optional<std::meta::info> {
        for (const auto annotation : std::meta::annotations_of(member)) {
            const auto type = std::meta::type_of(annotation);
            if (std::meta::has_template_arguments(type) && std::meta::template_of(type) == ^^Fk) {
                return type;
            }
        }
        return std::nullopt;
    }

    // True when `member` carries an fk<...> annotation (#431) — the FK-field predicate.
    consteval auto is_fk_field(std::meta::info member) -> bool {
        return fk_annotation_type_of(member).has_value();
    }

    // Text-length bound annotation (#493). A class-template annotation carries the
    // bound N as an NTTP (a FieldAttr enumerator cannot be templated, same as fk<Action>).
    // Enforced by the database on every write path: PG emits VARCHAR(N), SQLite emits
    // TEXT ... CHECK(length(col) <= N). Only a text field accepts it (std::string /
    // std::string_view / std::optional<those>) — a non-text field is a compile-time error
    // at the model boundary via ModelMaxLengthValid (statements/base.cppm).
    //   [[= storm::max_length<50>]] std::string name;
    template <std::size_t N> struct MaxLength {
        static constexpr std::size_t value = N;
    };
    // NOLINTNEXTLINE(readability-identifier-length) — `max_length` is the public annotation spelling
    template <std::size_t N> inline constexpr MaxLength<N> max_length{};

    // The bound N of a max_length<N> annotation on `member`, or std::nullopt when the
    // member carries none. Mirrors fk_annotation_type_of / fk_on_delete_action_of.
    consteval auto max_length_of(std::meta::info member) -> std::optional<std::size_t> {
        for (const auto annotation : std::meta::annotations_of(member)) {
            const auto type = std::meta::type_of(annotation);
            if (std::meta::has_template_arguments(type) && std::meta::template_of(type) == ^^MaxLength) {
                return std::meta::extract<std::size_t>(std::meta::template_arguments_of(type)[0]);
            }
        }
        return std::nullopt;
    }

    // True when the data member `member` has a text type (std::string / std::string_view),
    // looking through std::optional<>. Reflection-level type comparison (not `if constexpr`)
    // so it is usable inside a consteval loop where the loop variable is not a core constant
    // expression — the same discipline as is_unsigned64_member. Drives the max_length<N>
    // type guard: the annotation is only valid on a text field (#493). `std::string` /
    // `std::string_view` are using-declarators for basic_string<char> / basic_string_view<char>,
    // which cannot be reflected with ^^ directly; match on the class template plus a char
    // element instead. Both templates take char as their first argument, which is what
    // distinguishes the char specializations from the wide ones.
    consteval auto is_text_member(std::meta::info member) -> bool {
        std::meta::info t = std::meta::dealias(std::meta::type_of(member));
        if (is_optional_type(t)) {
            t = std::meta::dealias(std::meta::template_arguments_of(t)[0]);
        }
        if (!std::meta::has_template_arguments(t)) {
            return false;
        }
        const auto tmpl = std::meta::template_of(t);
        return (tmpl == ^^std::basic_string || tmpl == ^^std::basic_string_view) &&
               std::meta::dealias(std::meta::template_arguments_of(t)[0]) == ^^char;
    }

    // The "_id" suffix an FK column carries, paired with its byte length so the writer
    // (append_column_name) and the sizer (column_name_size) can never disagree (ES.45).
    inline constexpr std::string_view fk_id_suffix      = "_id";
    inline constexpr std::size_t      fk_id_suffix_size = fk_id_suffix.size();

    // Canonical column-name writer (#422): a member's SQL column is its identifier, plus
    // the "_id" suffix when the member is an FK (`User sender` → `sender_id`). This logic
    // was open-coded in every SQL builder (field_names, distinct, update, join, schema)
    // alongside a matching size-calculator that had to stay byte-exact. Route both through
    // this pair so the append and the size can never drift.
    //
    // `buf` is any buffer with `.append(std::string_view)` — the compile-time
    // ConstexprString and a runtime std::string both qualify. consteval (like every
    // builder that calls it) so it is constant-evaluated, never emitted as runtime code.
    consteval void append_column_name(auto& buf, std::meta::info member) {
        buf.append(std::meta::identifier_of(member));
        if (is_fk_field(member)) {
            buf.append(fk_id_suffix);
        }
    }

    // Byte size append_column_name() will emit for `member`: the identifier length, plus
    // the "_id" suffix length when the member is an FK. The exact companion to
    // append_column_name — every size-calculator that pairs with it must use this so the
    // buffer fits precisely.
    consteval auto column_name_size(std::meta::info member) -> std::size_t {
        return std::meta::identifier_of(member).size() + (is_fk_field(member) ? fk_id_suffix_size : 0);
    }

    // Composite-aware sibling of append_column_name/column_name_size (#422, widened
    // #504). A single-column FK target degenerates to EXACTLY append_column_name's
    // output ("<member>_id") — kept as a hardcoded special case (not derived from
    // the N>=2 branch) so the single-PK byte-identical guarantee can never drift.
    // A composite target spells the target part's own identifier into each column
    // name ("<member>_<part>"), since eliding it (as the single-column "_id" does)
    // would collide multiple parts into the same name.
    template <std::size_t N>
    consteval auto fk_column_names_size(
            std::meta::info                       fk_member,
            const std::array<std::meta::info, N>& target_pk_members,
            std::string_view                      separator
    ) -> std::size_t {
        if constexpr (N == 1) {
            return column_name_size(fk_member); // "<member>_id" — byte-identical to today
        } else {
            const auto  member_name = std::meta::identifier_of(fk_member);
            std::size_t total       = 0;
            for (std::size_t i = 0; i < N; ++i) {
                if (i > 0) {
                    total += separator.size();
                }
                total += member_name.size() + 1 + std::meta::identifier_of(target_pk_members[i]).size();
            }
            return total;
        }
    }

    template <typename Buf, std::size_t N>
    consteval auto append_fk_column_names(
            Buf&                                  buf,
            std::meta::info                       fk_member,
            const std::array<std::meta::info, N>& target_pk_members,
            std::string_view                      separator
    ) -> void {
        if constexpr (N == 1) {
            append_column_name(buf, fk_member); // "<member>_id" — byte-identical to today
        } else {
            const auto member_name = std::meta::identifier_of(fk_member);
            for (std::size_t i = 0; i < N; ++i) {
                if (i > 0) {
                    buf.append(separator);
                }
                buf.append(member_name);
                buf.append("_");
                buf.append(std::meta::identifier_of(target_pk_members[i]));
            }
        }
    }

    // Per-part siblings of fk_column_names_size/append_fk_column_names (#504), for callers
    // that emit each local-side FK column name separately rather than as one comma-joined
    // list — e.g. the JOIN ON clause, which AND-joins "t<alias>.<part> = t1.<fk>_<part>"
    // per target PK part instead of listing all local names together. N == 1 degenerates to
    // "<member>_id" (byte-identical to column_name_size/append_column_name), same as the
    // list form; part_index is unused in that branch (a single-column FK has exactly one
    // part, index 0).
    template <std::size_t N>
    consteval auto fk_column_name_size_for_part(
            std::meta::info fk_member, const std::array<std::meta::info, N>& target_pk_members, std::size_t part_index
    ) -> std::size_t {
        if constexpr (N == 1) {
            return column_name_size(fk_member); // "<member>_id" — byte-identical to today
        } else {
            return std::meta::identifier_of(fk_member).size() + 1 +
                   std::meta::identifier_of(target_pk_members[part_index]).size();
        }
    }

    template <typename Buf, std::size_t N>
    consteval auto append_fk_column_name_for_part(
            Buf&                                  buf,
            std::meta::info                       fk_member,
            const std::array<std::meta::info, N>& target_pk_members,
            std::size_t                           part_index
    ) -> void {
        if constexpr (N == 1) {
            append_column_name(buf, fk_member); // "<member>_id" — byte-identical to today
        } else {
            buf.append(std::meta::identifier_of(fk_member));
            buf.append("_");
            buf.append(std::meta::identifier_of(target_pk_members[part_index]));
        }
    }

    // The ON DELETE RefAction of an fk<...> FK, or std::nullopt when the field is not an FK
    // or carries the default RESTRICT (caller emits no clause then, keeping the
    // plain-REFERENCES DDL byte-identical).
    consteval auto fk_on_delete_action_of(std::meta::info member) -> std::optional<RefAction> {
        const auto type = fk_annotation_type_of(member);
        if (!type.has_value()) {
            return std::nullopt;
        }
        const auto action = std::meta::extract<RefAction>(std::meta::template_arguments_of(type.value())[0]);
        return action == RefAction::Restrict ? std::nullopt : std::optional<RefAction>(action);
    }

    // Issue #472: Entity is the compile-time STRUCTURAL gate for model types —
    // "is T a reflectable class type at all?". It is layered ABOVE the semantic
    // model concepts (ModelWithPrimaryKey / ModelStorageAnnotated /
    // ModelFkPoliciesValid in statements/base.cppm), which presuppose
    // reflectability. Constraining QuerySet<T> and BaseStatement<T> with it makes
    // a non-model T (int, a pointer, a function type) fail at this named boundary
    // instead of deep inside reflection-based code. is_class_type(^^T) is the gate
    // that actually rejects scalars/pointers/functions: in this clang-p2996 build
    // nonstatic_data_members_of(^^int, unchecked()) is NOT ill-formed (it yields an
    // empty std::vector<info>) and identifier_of(^^int) yields "int", so the two
    // structural requirements alone would accept int. The requirements are kept so
    // the concept still asserts T is reflectable the exact way the statement layer
    // uses it. access_context::unchecked() matches the semantic concepts' call
    // sites in statements/base.cppm.
    template <typename T>
    concept Entity = std::meta::is_class_type(^^T) && requires {
        { std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()) };
        { std::meta::identifier_of(^^T) } -> std::convertible_to<std::string_view>;
    };

} // namespace storm::meta
