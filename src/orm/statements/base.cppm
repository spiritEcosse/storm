module;

// Single cohesive class template; thresholds intentionally relaxed (see #264 finding).
// `duplicate` removed in #277 Phase 3 (bind_bulk_objects_impl + bind_expr_or_reset helpers; the
// for_each_field_name dedup now lives in storm_orm_statements_field_names, #434).

#include <meta>

export module storm_orm_statements_base;

import std;

import storm_db_concept;
import storm_orm_field_attr;
import storm_orm_utilities;
import storm_orm_statements_extract;
import storm_orm_statements_field_names;
import storm_orm_statements_orderby;
import storm_orm_where;

export namespace storm::orm::statements {

    // Import utilities for compile-time string building
    using storm::orm::utilities::ConstexprString;

    namespace meta {
        // Canonical FieldAttr + is_primary_attr live in the dependency-free
        // storm_orm_field_attr leaf module (#387); re-exposed here so statement
        // modules keep using the meta:: qualifier.
        using storm::meta::FieldAttr;
        using storm::meta::is_primary_attr;

        // Many-to-many annotation (#203). Phase 1 (auto-generated junction table):
        //   [[= storm::meta::many_to_many]] std::vector<Course> courses;
        // Phase 2 (explicit junction model):
        //   [[= storm::meta::many_to_many_through<Enrollment>]] std::vector<Course> courses;
        // FieldAttr is an enum, so a templated enumerator is impossible — a class-template
        // annotation carries the optional through-model type instead.
        template <typename Through = void> struct ManyToMany {
            using through_type = Through;
        };
        inline constexpr ManyToMany<>                                    many_to_many{};
        template <typename Through> inline constexpr ManyToMany<Through> many_to_many_through{};

        // Reflection of the ManyToMany<...> annotation TYPE carried by `member`, if any.
        consteval auto m2m_annotation_type_of(std::meta::info member) -> std::optional<std::meta::info> {
            for (const auto annotation : std::meta::annotations_of(member)) {
                const auto type = std::meta::type_of(annotation);
                if (std::meta::has_template_arguments(type) && std::meta::template_of(type) == ^^ManyToMany) {
                    return type;
                }
            }
            return std::nullopt;
        }

        consteval auto is_m2m_field(std::meta::info member) -> bool {
            return m2m_annotation_type_of(member).has_value();
        }

        // Free FieldAttr::fk predicate — same check as BaseStatement::is_fk_field, but
        // callable from the free join helpers (#398). Safe on members re-derived from
        // a model inside the instantiating TU (not on BMI-crossing NTTPs, #262).
        consteval auto is_fk_field(std::meta::info member) -> bool {
            auto attr = std::meta::annotation_of_type<FieldAttr>(member);
            return attr.has_value() && attr.value() == FieldAttr::fk;
        }

        // Reverse-FK annotation (#398): on a container member of the base model, it
        // declares the eager-load destination for "all <Base>, each with the <Owner>s
        // that point at them". The template argument is EITHER:
        //   - the owning model type — [[= reverse_fk<^^Task>]] vector<Task> tasks;
        //     (resolved to that model's unique FK back at the base), OR
        //   - a specific FK field — [[= reverse_fk<^^Task::assignee>]] vector<Task> tasks;
        //     (names the exact FK; the disambiguator when the owner has several FKs to
        //     the base, e.g. assignee vs reviewer).
        // The type form works when the owner is only forward-declared at the annotation
        // site (the cyclic Base⟷Owner case); the field form needs the owner complete.
        // Like ManyToMany, it is a class-template annotation (FieldAttr is an enum, so a
        // templated enumerator is impossible).
        template <std::meta::info Target> struct ReverseFk {
            static constexpr std::meta::info target = Target;
        };
        template <std::meta::info Target> inline constexpr ReverseFk<Target> reverse_fk{};

        // Reflection of the ReverseFk<...> annotation TYPE carried by `member`, if any.
        consteval auto reverse_fk_annotation_type_of(std::meta::info member) -> std::optional<std::meta::info> {
            for (const auto annotation : std::meta::annotations_of(member)) {
                const auto type = std::meta::type_of(annotation);
                if (std::meta::has_template_arguments(type) && std::meta::template_of(type) == ^^ReverseFk) {
                    return type;
                }
            }
            return std::nullopt;
        }

        consteval auto is_reverse_fk_field(std::meta::info member) -> bool {
            return reverse_fk_annotation_type_of(member).has_value();
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

        // True when the FK member `fk_member` (an FieldAttr::fk data member) points back
        // at base_t — its declared type, optional-unwrapped, is exactly base_t. The
        // single "does this FK reverse to the base?" check across the reverse-FK code.
        consteval auto fk_member_points_at(std::meta::info fk_member, std::meta::info base_t) -> bool {
            const auto fk_type = std::meta::dealias(std::meta::type_of(fk_member));
            return fk_type == base_t || (std::meta::has_template_arguments(fk_type) &&
                                         std::meta::template_of(fk_type) ==
                                                 std::meta::template_of(std::meta::dealias(^^std::optional<int>)) &&
                                         std::meta::dealias(std::meta::template_arguments_of(fk_type)[0]) == base_t);
        }

        // Count of FieldAttr::fk members of `owner` whose type points back at base_t.
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
        // base_t; a field target must be a FieldAttr::fk member of another model that
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
        // pointing back at base_t. A type target picks the unique FieldAttr::fk member
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

        // Combined relation-field predicate (#398): m2m container OR reverse_fk container.
        // The single chokepoint for "not a persisted column" — schema/CRUD invisibility
        // for both relation kinds falls out of every persisted-field array filtering here.
        consteval auto is_relation_field(std::meta::info member) -> bool {
            return is_m2m_field(member) || is_reverse_fk_field(member);
        }

        // True for m2m members WITHOUT a through model (auto-generated junction table).
        consteval auto is_m2m_auto(std::meta::info member) -> bool {
            auto type = m2m_annotation_type_of(member);
            return type.has_value() && std::meta::dealias(std::meta::template_arguments_of(type.value())[0]) == ^^void;
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

    // Concept: T must have at least one field annotated with FieldAttr::primary.
    //
    // Because a primary key is itself a non-static data member, satisfying this concept
    // also guarantees `field_count_ >= 1`. That invariant is what makes the INSERT batch
    // divides `MAX_DB_VARIABLES / field_count_` (insert.cppm) safe from division by zero —
    // the divisor can never be 0 for any T that reaches a statement class (issue #362, item A).
    template <typename T>
    concept ModelWithPrimaryKey = []() consteval {
        for (auto m : std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked())) {
            auto attr = std::meta::annotation_of_type<meta::FieldAttr>(m);
            if (attr.has_value() && meta::is_primary_attr(attr.value())) {
                return true;
            }
        }
        return false;
    }();

    // A field carrying auto_create/auto_update must be a system_clock::time_point (#209).
    // Referenced by a static_assert in bind_field_at_index so a wrong-typed timestamp field
    // fails to compile with a clear message rather than deep inside parameter binding.
    template <std::meta::info Member>
    concept ValidTimestampField = std::
            same_as<std::remove_cvref_t<typename[:std::meta::type_of(Member):]>, std::chrono::system_clock::time_point>;

    // A JOIN field selector must reflect a non-static data member of T annotated with
    // FieldAttr::fk (#388). Constrains QuerySet::join/left_join and
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
                auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(m);
                return field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk;
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

    // A cross-model reverse-FK selector (#398): Member is a FieldAttr::fk data member
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
        // Member must carry FieldAttr::fk — read from the member re-derived out of owner.
        for (auto m : std::meta::nonstatic_data_members_of(owner, std::meta::access_context::unchecked())) {
            if (std::meta::identifier_of(m) == std::meta::identifier_of(Member)) {
                auto attr = std::meta::annotation_of_type<meta::FieldAttr>(m);
                return attr.has_value() && attr.value() == meta::FieldAttr::fk;
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

    // A through model must carry exactly one FieldAttr::fk member of type Side
    // (#203 Phase 2) — that member names the junction FK column (<identifier>_id).
    template <typename Through, typename Side>
    concept ThroughWithFKTo = []() consteval {
        std::size_t count = 0;
        for (auto m : std::meta::nonstatic_data_members_of(^^Through, std::meta::access_context::unchecked())) {
            auto attr = std::meta::annotation_of_type<meta::FieldAttr>(m);
            if (attr.has_value() && attr.value() == meta::FieldAttr::fk &&
                std::meta::dealias(std::meta::type_of(m)) == std::meta::dealias(^^Side)) {
                ++count;
            }
        }
        return count == 1;
    }();

    // Shared reflection utilities for all statement types
    template <typename T>
        requires ModelWithPrimaryKey<T>
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
                auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
                if (field_attr.has_value() && meta::is_primary_attr(field_attr.value())) {
                    return member;
                }
            }
            std::unreachable(); // never reached: ModelWithPrimaryKey<T> guarantees a primary key exists
        }

        // FK field detection utilities
        static consteval auto is_fk_field(std::meta::info member) -> bool {
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            return field_attr.has_value() && field_attr.value() == meta::FieldAttr::fk;
        }

        // Unique field detection
        static consteval auto is_unique_field(std::meta::info member) -> bool {
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            return field_attr.has_value() && field_attr.value() == meta::FieldAttr::unique;
        }

        // Indexed field detection (explicit [[= FieldAttr::indexed]])
        static consteval auto is_indexed_field(std::meta::info member) -> bool {
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            return field_attr.has_value() && field_attr.value() == meta::FieldAttr::indexed;
        }

        // Auto-timestamp detection (#209): auto_create stamps now() on INSERT only;
        // auto_update stamps now() on both INSERT and UPDATE.
        static consteval auto is_auto_create_field(std::meta::info member) -> bool {
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            return field_attr.has_value() && field_attr.value() == meta::FieldAttr::auto_create;
        }

        static consteval auto is_auto_update_field(std::meta::info member) -> bool {
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            return field_attr.has_value() && field_attr.value() == meta::FieldAttr::auto_update;
        }

        // True when `member` should be stamped with now() on this operation: auto_update
        // always; auto_create on INSERT only (IsUpdate=false). auto_create on UPDATE is
        // false here so it binds the object's stored value (preserving created_at).
        static consteval auto stamps_now(std::meta::info member, bool is_update) -> bool {
            return is_auto_update_field(member) || (is_auto_create_field(member) && !is_update);
        }

        // Check if a field needs an index (indexed, unique, or fk — but not primary key)
        static consteval auto needs_index(std::meta::info member) -> bool {
            using enum meta::FieldAttr;
            if (member == primary_key_) {
                return false;
            }
            auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
            if (!field_attr.has_value()) {
                return false;
            }
            auto val = field_attr.value();
            return val == indexed || val == unique || val == fk;
        }

        // Get database column name for FK field: User sender → "sender_id"
        static consteval auto get_fk_column_name(std::meta::info member) -> std::string {
            const std::string field_name(std::meta::identifier_of(member));
            return field_name + "_id";
        }

      public:
        // Find primary key of a FK type (unwraps std::optional<T> → T first). Public so
        // the free two-query join helpers (join.cppm, #398) can extract FK columns.
        template <typename FKType>
            requires ModelWithPrimaryKey<utilities::optional_inner_type_t<FKType>>
        static consteval auto find_fk_primary_key() -> std::meta::info {
            using InnerType = utilities::optional_inner_type_t<FKType>;
            for (const std::meta::info member :
                 std::meta::nonstatic_data_members_of(^^InnerType, std::meta::access_context::unchecked())) {
                auto field_attr = std::meta::annotation_of_type<meta::FieldAttr>(member);
                if (field_attr.has_value() && meta::is_primary_attr(field_attr.value())) {
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

        // Unified field binder: binds a single field at compile-time index.
        // SkipPK=true skips primary key fields (for INSERT/UPDATE non-PK binding).
        // IsUpdate=true marks the UPDATE path so auto_create fields bind the object's
        // stored value instead of now() (#209). `now` is read once per operation by the
        // caller and threaded in so every row in a batch shares the same timestamp.
        // Auto-increments param_index on successful bind.
        template <typename ConnType, std::size_t Index, bool SkipPK = false, bool IsUpdate = false>
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

            // Compile-time PK skip for INSERT/UPDATE non-PK paths
            if constexpr (SkipPK && member == primary_key_) {
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

        // Common batch operation thresholds
        static constexpr std::size_t MAX_DB_VARIABLES = 999;

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

      protected:
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
            std::size_t pos = 0;
            while ((pos = adapted.find(" ASC", pos)) != std::string::npos) {
                std::size_t const after = pos + 4;
                if (adapted.substr(after, 6) != " NULLS") {
                    adapted.insert(after, " NULLS FIRST");
                }
                pos = after + 12;
            }
            pos = 0;
            while ((pos = adapted.find(" DESC", pos)) != std::string::npos) {
                std::size_t const after = pos + 5;
                if (adapted.substr(after, 6) != " NULLS") {
                    adapted.insert(after, " NULLS LAST");
                }
                pos = after + 11;
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
                if constexpr (requires { ConnTypeForDialect::uses_pg_dialect; }) {
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
                if constexpr (requires { ConnTypeForDialect::supports_limit_all; }) {
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
